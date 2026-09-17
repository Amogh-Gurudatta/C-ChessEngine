/*
 * ======================================================================================
 * File: ai.c
 * Description: Implementation of the Chess Engine Artificial Intelligence.
 *
 * Key Algorithms:
 * 1. Pure NegaMax:
 * - Simplifies the search logic by treating every node as "Maximizing" for the
 * current player.
 * - White maximizes (White - Black).
 * - Black maximizes (Black - White).
 * - This fixes the "suicide" bug where Black was maximizing White's advantage.
 *
 * 2. Alpha-Beta Pruning:
 * - Reduces search space by cutting off branches that are mathematically
 * proven to be worse than what we have already found.
 *
 * 3. Quiescence Search:
 * - Resolves the "Horizon Effect" by playing out captures at the end of the
 * search to reach a stable position.
 *
 * 4. MVV-LVA Move Ordering:
 * - "Most Valuable Victim - Least Valuable Aggressor".
 * - Prioritizes examining good captures first to improve pruning efficiency.
 * ======================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "ai.h"
#include "book.h"
#include "eval.h"
#include "game.h"
#include "structs.h"

/* * DEFAULT_SEARCH_DEPTH: The default number of half-moves (plies) the engine
 * searches. Depth 6 allows the engine to see 3 full moves ahead for both
 * sides. Adjustable at runtime via setSearchDepth() (see ai.h).
 */
#define DEFAULT_SEARCH_DEPTH 6
#define DEFAULT_SEARCH_TIME_LIMIT 5.0 /* seconds; <= 0 disables the cap */
#define INFINITY_SCORE 1000000
#define MATE_VALUE (INFINITY_SCORE - 1000)

/* findBestMoveTimed()'s depth ceiling while a real clock is in effect: a
 * real clock's time budget, not a fixed plies ceiling, should be what
 * stops the search. Without this, whatever getSearchDepth() happens to be
 * set to (DEFAULT_SEARCH_DEPTH's 6, if nothing raised it) finishes almost
 * instantly on modern hardware and returns long before the computed time
 * budget is actually used, so the engine plays far faster - and weaker -
 * than the clock would otherwise allow. Kept comfortably below
 * MAX_KILLER_PLY (128, see below) to leave slack for check-extension
 * stacking. */
#define TIMED_SEARCH_MAX_DEPTH 64

/* Null-move pruning (see the NULL-MOVE PRUNING section below): only tried
 * with at least this much depth left, reduced by this many extra plies
 * beyond the normal depth-1 recursion. NULL_MOVE_MIN_DEPTH is deliberately
 * NULL_MOVE_REDUCTION + 2, not just + 1: that guarantees the reduced probe
 * always retains at least one real ply of full search before quiescence
 * (depth - 1 - NULL_MOVE_REDUCTION >= 1), rather than sometimes dropping
 * straight into quiescence - a bare stand-pat "verification" is far too
 * cheap/unreliable a test and was measured to cause wildly excessive false
 * cutoffs (thousands-of-times too few nodes searched at some depths) before
 * this margin was added. See docs/SEARCH_AND_EVAL.md. */
#define NULL_MOVE_REDUCTION 2
#define NULL_MOVE_MIN_DEPTH (NULL_MOVE_REDUCTION + 2)

/* Late move reductions (see LMR section below): the first LMR_MIN_MOVE_INDEX
 * candidates (the TT move, captures, killers - already well-ordered by
 * scoreMoves()) always get a full-depth search; later quiet moves get a
 * cheap reduced-depth probe first, re-searched at full depth only if that
 * probe unexpectedly beats alpha. */
#define LMR_MIN_DEPTH 3
#define LMR_MIN_MOVE_INDEX 4
#define LMR_REDUCTION 1

static int searchDepth = DEFAULT_SEARCH_DEPTH;
static double searchTimeLimitSeconds = DEFAULT_SEARCH_TIME_LIMIT;
static clock_t searchStartTime;
static bool searchAborted;

void setSearchDepth(int depth)
{
    if (depth >= 1)
        searchDepth = depth;
}

int getSearchDepth(void)
{
    return searchDepth;
}

void setSearchTimeLimit(double seconds)
{
    searchTimeLimitSeconds = seconds;
}

double getSearchTimeLimit(void)
{
    return searchTimeLimitSeconds;
}

/**
 * @brief True once the search has been running longer than the configured
 * time limit. A limit of 0 or less means "no cap".
 */
static bool searchTimeExpired(void)
{
    if (searchTimeLimitSeconds <= 0)
        return false;
    double elapsedSeconds = (double)(clock() - searchStartTime) / CLOCKS_PER_SEC;
    return elapsedSeconds >= searchTimeLimitSeconds;
}

/* Checking the clock on every single node would add needless overhead in
 * tight tactical sequences (quiescence can visit many nodes very quickly),
 * so it's only actually polled once every TIME_CHECK_INTERVAL calls. */
#define TIME_CHECK_INTERVAL 2048
static long nodesSinceTimeCheck = 0;

static bool searchShouldStop(void)
{
    if (searchAborted)
        return true;

    if (++nodesSinceTimeCheck >= TIME_CHECK_INTERVAL)
    {
        nodesSinceTimeCheck = 0;
        if (searchTimeExpired())
            searchAborted = true;
    }
    return searchAborted;
}

/* Total nodes visited (negamax + quiescence calls) during the most recent
 * findBestMove()/findBestMoveTimed() call. A diagnostic, not used for any
 * search decision - mainly so the transposition table's effect (fewer
 * nodes for the same depth) can actually be observed/tested. */
static long nodesSearched = 0;

long getLastSearchNodeCount(void)
{
    return nodesSearched;
}

/* ========================================================================== */
/* TRANSPOSITION TABLE (Zobrist hashing)                                     */
/* ========================================================================== */
/* See docs/SEARCH_AND_EVAL.md for the design rationale (why the hash is
 * recomputed from scratch each node instead of maintained incrementally,
 * why quiescence() doesn't use it, and the mate-score ply-adjustment). */

#define ZOBRIST_SEED 0x2545F4914F6CDD1DULL

static uint64_t zobristPieceSquare[2][7][64]; /* [color][PieceType][row*8+col]; index 0 (EMPTY) unused */
static uint64_t zobristSideToMove;
static uint64_t zobristCastling[4]; /* wk, wq, bk, bq */
static uint64_t zobristEnPassantFile[8];
static bool zobristInitialized = false;

/* A small, fixed-seed PRNG (splitmix64) so Zobrist keys - and therefore
 * every search result - are fully reproducible from run to run. This
 * matters for tests: a time-seeded PRNG could (rarely, among otherwise
 * equal-scoring moves) make the engine's move ordering, and hence its
 * choice among ties, non-deterministic. */
static uint64_t splitmix64Next(uint64_t *state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void zobristEnsureInit(void)
{
    if (zobristInitialized)
        return;

    uint64_t state = ZOBRIST_SEED;
    for (int color = 0; color < 2; color++)
        for (int type = 0; type < 7; type++)
            for (int sq = 0; sq < 64; sq++)
                zobristPieceSquare[color][type][sq] = splitmix64Next(&state);

    zobristSideToMove = splitmix64Next(&state);
    for (int i = 0; i < 4; i++)
        zobristCastling[i] = splitmix64Next(&state);
    for (int i = 0; i < 8; i++)
        zobristEnPassantFile[i] = splitmix64Next(&state);

    zobristInitialized = true;
}

/* Recomputed from scratch each call (a plain 64-square scan) rather than
 * maintained incrementally inside makeMove()/undoMove(). That's slower per
 * node in isolation, but far simpler and safer: an incrementally-updated
 * hash has to be threaded through every one of makeMove's special cases
 * (castling, en passant, promotion, captured rooks revoking rights, ...)
 * and silently drifting out of sync there would cause wrong-but-plausible
 * search results that are extremely hard to notice, let alone debug. */
static uint64_t zobristHash(BoardState *board)
{
    zobristEnsureInit();

    uint64_t hash = 0;
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.type != EMPTY)
                hash ^= zobristPieceSquare[p.color][p.type][r * 8 + c];
        }
    }

    if (board->currentPlayer == BLACK)
        hash ^= zobristSideToMove;
    if (board->castling.wk)
        hash ^= zobristCastling[0];
    if (board->castling.wq)
        hash ^= zobristCastling[1];
    if (board->castling.bk)
        hash ^= zobristCastling[2];
    if (board->castling.bq)
        hash ^= zobristCastling[3];
    if (board->enPassantTarget.row != -1)
        hash ^= zobristEnPassantFile[board->enPassantTarget.col];

    return hash;
}

typedef enum
{
    TT_FLAG_EXACT,
    TT_FLAG_LOWERBOUND,
    TT_FLAG_UPPERBOUND
} TTFlag;

typedef struct
{
    uint64_t key;
    int depth;
    int score;
    TTFlag flag;
    Move bestMove;
    bool occupied;
} TTEntry;

/* 2^18 entries (~12MB). Power-of-two sized so the index is a cheap mask
 * instead of a modulo. */
#define TT_SIZE_BITS 18
#define TT_SIZE (1u << TT_SIZE_BITS)
#define TT_INDEX_MASK (TT_SIZE - 1)

static TTEntry transpositionTable[TT_SIZE];
static bool useTranspositionTable = true;

void setUseTranspositionTable(bool enabled)
{
    useTranspositionTable = enabled;
}

bool getUseTranspositionTable(void)
{
    return useTranspositionTable;
}

/* Mate scores (see MATE_VALUE above) encode "distance to mate from the
 * root of the CURRENT search call" by construction (negamax returns
 * -MATE_VALUE + ply at a mated leaf). That makes them meaningless once
 * cached: the same position reached at a different ply - via a different
 * move order, or in a later search entirely - would wrongly inherit a
 * mate distance measured from a different root. Converting to/from a
 * ply-independent form before storing/after loading (the standard
 * transposition-table technique) fixes this: two calls that cancel out
 * whenever the value is used at the SAME ply it was computed at, but
 * correctly re-relativize it when reused at a different one. */
#define MATE_SCORE_THRESHOLD (MATE_VALUE - 1000)

static int mateScoreToTT(int score, int ply)
{
    if (score >= MATE_SCORE_THRESHOLD)
        return score + ply;
    if (score <= -MATE_SCORE_THRESHOLD)
        return score - ply;
    return score;
}

static int mateScoreFromTT(int score, int ply)
{
    if (score >= MATE_SCORE_THRESHOLD)
        return score - ply;
    if (score <= -MATE_SCORE_THRESHOLD)
        return score + ply;
    return score;
}

/* Returns NULL on a miss (including when the table is disabled). A hit
 * only means "this position has been searched before" - the caller still
 * has to check .depth before trusting .score for a cutoff; .bestMove is
 * safe to use as a move-ordering hint regardless of stored depth. */
static TTEntry *ttLookup(uint64_t key)
{
    if (!useTranspositionTable)
        return NULL;
    TTEntry *entry = &transpositionTable[key & TT_INDEX_MASK];
    if (entry->occupied && entry->key == key)
        return entry;
    return NULL;
}

/* Depth-preferred replacement: a shallower re-search of the same position
 * never overwrites a deeper, more valuable entry already there. */
static void ttStore(uint64_t key, int depth, int score, TTFlag flag, Move bestMove)
{
    if (!useTranspositionTable)
        return;
    TTEntry *entry = &transpositionTable[key & TT_INDEX_MASK];
    if (entry->occupied && entry->key == key && entry->depth > depth)
        return;

    entry->key = key;
    entry->depth = depth;
    entry->score = score;
    entry->flag = flag;
    entry->bestMove = bestMove;
    entry->occupied = true;
}

static bool movesEqual(Move a, Move b)
{
    return a.from.row == b.from.row && a.from.col == b.from.col &&
           a.to.row == b.to.row && a.to.col == b.to.col &&
           a.promotion == b.promotion;
}

/* ========================================================================== */
/* MOVE ORDERING STATE (killer moves + history heuristic)                    */
/* ========================================================================== */

/* Two killer slots per ply: quiet moves that recently caused a beta cutoff
 * at that same ply, in a sibling branch. Tried right after captures/
 * promotions, on the reasoning that a move which refuted one line is a
 * good first guess for refuting a similar sibling line too. Bounded (and
 * always bounds-checked) since ply can in principle exceed any fixed size
 * if the search depth is set very high and check extensions stack. */
#define MAX_KILLER_PLY 128
static Move killerMoves[MAX_KILLER_PLY][2];

/* Quiet moves that have caused cutoffs anywhere in the current search,
 * indexed by [from-square][to-square] and weighted by depth^2 (a cutoff
 * found deep in the tree is a stronger signal than one found near a leaf).
 * Reset per findBestMove() call - it's a hint for the *current* search,
 * not something that should bias an unrelated later position. */
static int historyTable[64][64];

static void resetMoveOrderingState(void)
{
    Move invalid = {.from = {-1, -1}, .to = {-1, -1}, .promotion = EMPTY, .flag = MOVE_NORMAL};
    for (int p = 0; p < MAX_KILLER_PLY; p++)
    {
        killerMoves[p][0] = invalid;
        killerMoves[p][1] = invalid;
    }
    memset(historyTable, 0, sizeof(historyTable));
}

static void recordQuietCutoff(Move m, int ply, int depth)
{
    if (ply >= 0 && ply < MAX_KILLER_PLY && !movesEqual(m, killerMoves[ply][0]))
    {
        killerMoves[ply][1] = killerMoves[ply][0];
        killerMoves[ply][0] = m;
    }
    historyTable[m.from.row * 8 + m.from.col][m.to.row * 8 + m.to.col] += depth * depth;
}

/* ========================================================================== */
/* NULL-MOVE PRUNING & LATE MOVE REDUCTIONS                                  */
/* ========================================================================== */
/* See docs/SEARCH_AND_EVAL.md for the full reasoning; both default enabled
 * like every other search refinement here (TT, killers/history, opening
 * book) - these toggles exist mainly so tests can compare node counts with
 * each on vs. off. */

static bool useNullMovePruning = true;
static bool useLateMoveReductions = true;

void setUseNullMovePruning(bool enabled)
{
    useNullMovePruning = enabled;
}

bool getUseNullMovePruning(void)
{
    return useNullMovePruning;
}

void setUseLateMoveReductions(bool enabled)
{
    useLateMoveReductions = enabled;
}

bool getUseLateMoveReductions(void)
{
    return useLateMoveReductions;
}

/* True if `color` has any piece besides its king and pawns - the standard
 * zugzwang safeguard for null-move pruning: in a position with only king
 * and pawns left, passing can genuinely be the best option (the losing
 * side is often in zugzwang), so the whole "a free move can't help the
 * opponent more than beta" premise null-move pruning relies on breaks
 * down. Skipping it whenever this is false is the simple, standard
 * mitigation (a full verification search is a further refinement, not
 * needed here). */
static bool hasNonPawnMaterial(BoardState *board, PieceColor color)
{
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.color == color && p.type != PAWN && p.type != KING)
                return true;
        }
    return false;
}

/* Makes/undoes a "null move": passing the turn without moving a piece, used
 * only as null-move pruning's hypothetical probe, never a played move -
 * halfmoveClock/fullmoveNumber are deliberately left untouched. A real move
 * always consumes or invalidates enPassantTarget, so a null move must too;
 * the caller restores it via undoNullMove(). zobristHash()'s existing
 * recompute-from-scratch design already reflects both changes for free. */
static void makeNullMove(BoardState *board, Position *savedEnPassant)
{
    *savedEnPassant = board->enPassantTarget;
    board->enPassantTarget = (Position){-1, -1};
    board->currentPlayer = (board->currentPlayer == WHITE) ? BLACK : WHITE;
}

static void undoNullMove(BoardState *board, Position savedEnPassant)
{
    board->currentPlayer = (board->currentPlayer == WHITE) ? BLACK : WHITE;
    board->enPassantTarget = savedEnPassant;
}

/* -------------------------------------------------------------------------- */
/* INTERNAL FUNCTION PROTOTYPES                                               */
/* -------------------------------------------------------------------------- */

/* Core Search Logic */

static int negamax(BoardState *board, int depth, int alpha, int beta, int ply, bool allowNullMove);
static int quiescence(BoardState *board, int alpha, int beta);

/* Heuristics & Ordering */

static int scoreMove(BoardState *board, Move m, int ply, Move ttMoveHint);
static void scoreMoves(BoardState *board, MoveList *list, int ply, Move ttMoveHint);

/* Move Generation Helpers (Standard Chess Logic) */

static void generatePseudoLegalMoves(BoardState *board, MoveList *list);
static void generatePawnMoves(BoardState *board, MoveList *list, int r, int c);
static void generateKnightMoves(BoardState *board, MoveList *list, int r, int c);
static void generateKingMoves(BoardState *board, MoveList *list, int r, int c);
static void generateSlidingMoves(BoardState *board, MoveList *list, int r, int c);
static void addMove(BoardState *board, MoveList *list, Move move);

/* ========================================================================== */
/* 1. ROOT MOVE SEARCH (Entry Point)                                          */
/* ========================================================================== */

/* See ai.h for findBestMove's contract. Implementation note: iterative
 * deepening (depth 1, 2, 3, ... up to searchDepth) is what lets a time cap
 * (setSearchTimeLimit) interrupt the search between/during depths and
 * still return a sound move, instead of either blocking indefinitely at a
 * high depth or having no time awareness at all. */
Move findBestMove(BoardState *board)
{
    searchStartTime = clock();
    searchAborted = false;
    nodesSinceTimeCheck = 0;
    nodesSearched = 0;
    resetMoveOrderingState();

    Move noHint = {.from = {-1, -1}, .to = {-1, -1}, .promotion = EMPTY, .flag = MOVE_NORMAL};

    Move bestMove;
    bestMove.from = (Position){-1, -1}; // Initialize to invalid to detect errors

    // 1. Generate all legal moves
    MoveList legalMoves = generateAllLegalMoves(board);
    if (legalMoves.count == 0)
        return bestMove;

    // Guarantee a legal move is always returned, even if the very first
    // depth gets interrupted before finishing.
    bestMove = legalMoves.moves[0];

    // Opening book: if the current position is known theory, play its
    // suggested continuation immediately instead of spending search time
    // re-deriving it. The book only ever returns a plain from/to (see
    // book.h) - re-resolved here against the real legal move list rather
    // than trusted outright, so it can never return anything but a fully
    // legal move even in the (extremely unlikely, see docs/OPENING_BOOK.md)
    // case of its position key matching a position it wasn't built for.
    Move bookRaw;
    if (findBookMove(board, &bookRaw))
    {
        for (int i = 0; i < legalMoves.count; i++)
        {
            Move m = legalMoves.moves[i];
            if (m.from.row == bookRaw.from.row && m.from.col == bookRaw.from.col &&
                m.to.row == bookRaw.to.row && m.to.col == bookRaw.to.col)
                return m;
        }
    }

    // Sort moves: Check Captures first! Finding a good move early allows
    // Alpha-Beta to prune bad branches later. If a previous, unrelated
    // search already analyzed this exact position, its transposition-table
    // entry gives a first guess at the best move too.
    uint64_t rootHash = zobristHash(board);
    TTEntry *rootEntry = ttLookup(rootHash);
    scoreMoves(board, &legalMoves, 0, rootEntry != NULL ? rootEntry->bestMove : noHint);

    for (int depth = 1; depth <= searchDepth; depth++)
    {
        int alpha = -INFINITY_SCORE;
        int beta = INFINITY_SCORE;
        int bestValThisDepth = -INFINITY_SCORE;
        Move bestMoveThisDepth = bestMove;
        bool depthCompleted = true;

        for (int i = 0; i < legalMoves.count; i++)
        {
            Move currentMove = legalMoves.moves[i];

            makeMove(board, currentMove);

            /* * RECURSIVE CALL (NegaMax Variant):
             * value = -negamax(...)
             * We flip the result because the opponent's score is bad for us.
             * We swap -beta and -alpha to reflect the perspective shift.
             */
            int val = -negamax(board, depth - 1, -beta, -alpha, 1, true);

            undoMove(board, currentMove);

            if (searchAborted)
            {
                // This depth's results are incomplete/unreliable - discard
                // them and keep whatever the last full depth found.
                depthCompleted = false;
                break;
            }

            if (val > bestValThisDepth)
            {
                bestValThisDepth = val;
                bestMoveThisDepth = currentMove;
            }

            if (val > alpha)
            {
                alpha = val;
            }
        }

        if (depthCompleted)
        {
            bestMove = bestMoveThisDepth;

            // Re-order for the next, deeper iteration: trying this depth's
            // best move first again is the strongest ordering hint
            // available, keeping alpha-beta pruning effective as depth grows.
            scoreMoves(board, &legalMoves, 0, bestMove);
        }

        if (searchAborted || searchTimeExpired())
            break;
    }

    return bestMove;
}

/* ========================================================================== */
/* 1B. CLOCK-AWARE TIME MANAGEMENT                                           */
/* ========================================================================== */

/* See ai.h for computeMoveTimeBudget's contract. The formula below (bank
 * most of the increment, weight by game phase, clamp to a safe fraction of
 * what's left, and panic once critically low) is spelled out step by step
 * in docs/SEARCH_AND_EVAL.md. */
double computeMoveTimeBudget(BoardState *board, double remainingSeconds, double incrementSeconds)
{
    if (remainingSeconds <= 0)
        return 0.02; // already effectively out of time; still must return *some* move

    int phase = getGamePhase(board); // 0 (bare kings) .. 24 (full material)

    // More material on the board generally means more moves are likely still
    // to be played; bare endgames tend to resolve faster (often forced).
    int movesToGo = 20 + (phase * 20) / 24; // ranges 20 (bare) .. 40 (full material)

    double budget = (remainingSeconds / movesToGo) + (incrementSeconds * 0.8);

    // The richest tactical complexity is usually in the middlegame, so spend
    // a bit more there; spend less once things have simplified toward a bare
    // endgame, where play is often more forced and needs less calculation.
    double phaseFactor = 1.0;
    if (phase >= 8 && phase <= 20)
        phaseFactor = 1.2;
    else if (phase < 4)
        phaseFactor = 0.7;
    budget *= phaseFactor;

    // Never risk more than a safe fraction of what's left on a single move.
    double maxSafe = remainingSeconds * 0.4;
    if (budget > maxSafe)
        budget = maxSafe;

    // Panic mode: once critically low on time, spend only a sliver of it so
    // there's always time left to make the next several moves too.
    const double panicThresholdSeconds = 5.0;
    if (remainingSeconds < panicThresholdSeconds)
    {
        double panicBudget = remainingSeconds * 0.2;
        if (budget > panicBudget)
            budget = panicBudget;
    }

    if (budget < 0.02)
        budget = 0.02;

    return budget;
}

/* See ai.h for findBestMoveTimed's contract. */
Move findBestMoveTimed(BoardState *board, double remainingSeconds, double incrementSeconds)
{
    double budget = computeMoveTimeBudget(board, remainingSeconds, incrementSeconds);

    double previousLimit = searchTimeLimitSeconds;
    setSearchTimeLimit(budget);

    // See TIMED_SEARCH_MAX_DEPTH: only raise it, never lower it, in case
    // the caller had deliberately set an even higher ceiling already.
    int previousDepth = searchDepth;
    if (searchDepth < TIMED_SEARCH_MAX_DEPTH)
        setSearchDepth(TIMED_SEARCH_MAX_DEPTH);

    Move best = findBestMove(board);

    setSearchDepth(previousDepth);
    setSearchTimeLimit(previousLimit);

    return best;
}

/* ========================================================================== */
/* 2. PURE NEGAMAX SEARCH ALGORITHMS                                          */
/* ========================================================================== */

/**
 * @brief Quiescence Search (NegaMax Style)
 * Called at the leaf nodes of the main search.
 * It continues searching ONLY capture moves to resolve tactical instability.
 * * @param alpha Lower bound score.
 * @param beta Upper bound score.
 * @return The evaluation score relative to the side to move.
 */
static int quiescence(BoardState *board, int alpha, int beta)
{
    nodesSearched++;
    if (searchShouldStop())
        return 0;

    // 1. STAND-PAT:
    // Get the static score of the board.
    // evaluateBoard() returns (White - Black).
    // NegaMax requires (Me - Opponent).
    // If I am Black, flip the score.
    int stand_pat = evaluateBoard(board);
    if (board->currentPlayer == BLACK)
        stand_pat = -stand_pat;

    // 2. Beta Cutoff: If standing pat is already too good, return beta.
    if (stand_pat >= beta)
        return beta;

    // 3. Alpha Update: If standing pat is better than alpha, raise the floor.
    if (stand_pat > alpha)
        alpha = stand_pat;

    // 4. GENERATE MOVES (Captures Only)
    // Quiescence doesn't use the transposition table or killer/history
    // ordering (see docs/SEARCH_AND_EVAL.md) - plain MVV-LVA only, same as
    // before, hence the out-of-range ply and empty move hint below.
    MoveList moves = generateAllLegalMoves(board);
    Move noHint = {.from = {-1, -1}, .to = {-1, -1}, .promotion = EMPTY, .flag = MOVE_NORMAL};
    scoreMoves(board, &moves, -1, noHint);

    for (int i = 0; i < moves.count; i++)
    {
        Move m = moves.moves[i];

        // FILTER: Check destination square.
        // If empty and not En Passant, it's a Quiet move -> Skip it.
        Piece target = board->squares[m.to.row][m.to.col];
        if (target.type == EMPTY && m.flag != MOVE_EN_PASSANT)
            continue;

        makeMove(board, m);

        // Recursion: -quiescence (Flip perspective)
        int score = -quiescence(board, -beta, -alpha);

        undoMove(board, m);

        // Pruning
        if (score >= beta)
            return beta;
        if (score > alpha)
            alpha = score;
    }
    return alpha;
}

/**
 * @brief Standard NegaMax Alpha-Beta Search.
 * @param depth Remaining depth to search.
 * @param alpha Best score maximizer can guarantee.
 * @param beta Best score minimizer can guarantee.
 * @return The evaluation score relative to the side to move.
 */
static int negamax(BoardState *board, int depth, int alpha, int beta, int ply, bool allowNullMove)
{
    nodesSearched++;
    if (searchShouldStop())
        return 0;

    // BASE CASE 1: Draw Rules (50-move rule or Insufficient Material)
    if (board->halfmoveClock >= 100 || isInsufficientMaterial(board))
        return 0;

    // CHECK EXTENSION
    // If we are in check, we extend the search depth by 1.
    // This ensures we don't stop searching just before a checkmate.
    // Computed before the transposition-table probe below so a stored
    // entry's depth always means the same thing (post-extension) whether
    // it's being read or written.
    bool inCheck = isKingInCheck(board, board->currentPlayer);
    if (inCheck)
    {
        depth++;
    }

    int origAlpha = alpha;
    uint64_t hash = zobristHash(board);
    Move noHint = {.from = {-1, -1}, .to = {-1, -1}, .promotion = EMPTY, .flag = MOVE_NORMAL};
    Move ttMoveHint = noHint;

    TTEntry *tte = ttLookup(hash);
    if (tte != NULL)
    {
        ttMoveHint = tte->bestMove;
        if (tte->depth >= depth)
        {
            int score = mateScoreFromTT(tte->score, ply);
            if (tte->flag == TT_FLAG_EXACT)
                return score;
            if (tte->flag == TT_FLAG_LOWERBOUND && score >= beta)
                return score;
            if (tte->flag == TT_FLAG_UPPERBOUND && score <= alpha)
                return score;
        }
    }

    // BASE CASE 2: Depth Limit Reached -> Enter Quiescence Search
    if (depth <= 0)
        return quiescence(board, alpha, beta);

    // NULL-MOVE PRUNING: give the opponent a free move and see if they
    // still can't beat beta even with it. If they can't, our actual
    // position is safely at least that good, and this whole subtree can be
    // pruned. Guarded against check (can't legally pass), zugzwang
    // (hasNonPawnMaterial), doing two null moves in a row (allowNullMove),
    // and being too shallow to be worth it. See docs/SEARCH_AND_EVAL.md.
    if (useNullMovePruning && allowNullMove && !inCheck && depth >= NULL_MOVE_MIN_DEPTH &&
        hasNonPawnMaterial(board, board->currentPlayer))
    {
        Position savedEnPassant;
        makeNullMove(board, &savedEnPassant);
        int nullScore = -negamax(board, depth - 1 - NULL_MOVE_REDUCTION, -beta, -beta + 1, ply + 1, false);
        undoNullMove(board, savedEnPassant);

        if (nullScore >= beta)
            return beta;
    }

    // Generate Moves
    MoveList legalMoves = generateAllLegalMoves(board);

    // BASE CASE 3: End of Game (Checkmate or Stalemate)
    if (legalMoves.count == 0)
    {
        if (inCheck)
            // Checkmate: Return -MATE + ply.
            // Faster mates (lower ply) result in higher scores for the winner.
            return -MATE_VALUE + ply;
        else
            // Stalemate
            return 0;
    }

    // Sort Moves (Captures first for pruning; the transposition table's
    // suggested move, if any, is tried before even those).
    scoreMoves(board, &legalMoves, ply, ttMoveHint);

    // RECURSION
    int maxVal = -INFINITY_SCORE;
    Move bestMoveHere = legalMoves.moves[0];

    for (int i = 0; i < legalMoves.count; i++)
    {
        Move currentMove = legalMoves.moves[i];
        bool isQuiet = (board->squares[currentMove.to.row][currentMove.to.col].type == EMPTY &&
                        currentMove.flag != MOVE_EN_PASSANT);

        makeMove(board, currentMove);

        int score;
        bool skipFullDepthSearch = false;

        // LATE MOVE REDUCTIONS: this far into an already-well-ordered move
        // list (past the TT move, captures, promotions, and killers - see
        // scoreMove() below), a quiet move is statistically unlikely to be
        // best. Try it at a reduced depth first; only pay for the full-
        // depth search below if that cheap probe unexpectedly beats alpha.
        // Skipped near check (an unreliable moment for a shallow probe) and
        // once depth is already too shallow to be worth reducing further.
        // See docs/SEARCH_AND_EVAL.md.
        if (useLateMoveReductions && i >= LMR_MIN_MOVE_INDEX && depth >= LMR_MIN_DEPTH && isQuiet && !inCheck)
        {
            int reducedDepth = depth - 1 - LMR_REDUCTION;
            if (reducedDepth < 0)
                reducedDepth = 0;
            score = -negamax(board, reducedDepth, -beta, -alpha, ply + 1, true);
            skipFullDepthSearch = (score <= alpha);
        }

        // NegaMax Step: Flip alpha/beta, negate result.
        if (!skipFullDepthSearch)
            score = -negamax(board, depth - 1, -beta, -alpha, ply + 1, true);

        undoMove(board, currentMove);

        // Track best score
        if (score > maxVal)
        {
            maxVal = score;
            bestMoveHere = currentMove;
        }

        // Update Alpha
        if (score > alpha)
            alpha = score;

        // Beta Pruning: Opponent has a better option elsewhere.
        if (alpha >= beta)
        {
            // A quiet move that refuted this line is a good first guess for
            // refuting a similar sibling line too (killer moves), and a
            // useful general signal for move ordering elsewhere (history).
            if (isQuiet)
                recordQuietCutoff(currentMove, ply, depth);
            break;
        }
    }

    TTFlag flag = (maxVal <= origAlpha) ? TT_FLAG_UPPERBOUND : (maxVal >= beta) ? TT_FLAG_LOWERBOUND
                                                                                : TT_FLAG_EXACT;
    ttStore(hash, depth, mateScoreToTT(maxVal, ply), flag, bestMoveHere);

    return maxVal;
}

/* ========================================================================== */
/* 3. HEURISTICS & HELPERS (MVV-LVA)                                          */
/* ========================================================================== */

/**
 * @brief Assigns a score to a move for sorting purposes, so alpha-beta sees
 * the most promising moves first and prunes more of the tree. Ordered,
 * highest priority first: the transposition table's suggested move (our
 * best guess at the best move here, from a previous search of this exact
 * position), then MVV-LVA captures, then promotions, then killer moves
 * (quiet moves that recently refuted a sibling line at this same ply),
 * then other quiet moves by history-heuristic score.
 * * @param ply Search ply, for killer-move lookup; pass a negative value
 * (e.g. from quiescence(), which doesn't use killers) to skip it safely.
 * @param ttMoveHint The transposition table's suggested move, or a move
 * with from.row == -1 if none.
 * @return Higher score = better candidate to search first.
 */
static int scoreMove(BoardState *board, Move m, int ply, Move ttMoveHint)
{
    if (ttMoveHint.from.row != -1 && movesEqual(m, ttMoveHint))
        return 1000000;

    Piece target = board->squares[m.to.row][m.to.col];

    // A. CAPTURES
    if (target.type != EMPTY)
    {
        int victimVal = 0;
        int attackerVal = 0;

        switch (target.type)
        {
        case PAWN:
            victimVal = 100;
            break;
        case KNIGHT:
            victimVal = 320;
            break;
        case BISHOP:
            victimVal = 330;
            break;
        case ROOK:
            victimVal = 500;
            break;
        case QUEEN:
            victimVal = 900;
            break;
        case KING:
            victimVal = 20000;
            break;
        default:
            break;
        }

        Piece attacker = board->squares[m.from.row][m.from.col];
        switch (attacker.type)
        {
        case PAWN:
            attackerVal = 100;
            break;
        case KNIGHT:
            attackerVal = 320;
            break;
        case BISHOP:
            attackerVal = 330;
            break;
        case ROOK:
            attackerVal = 500;
            break;
        case QUEEN:
            attackerVal = 900;
            break;
        case KING:
            attackerVal = 20000;
            break;
        default:
            break;
        }

        // Score Formula: Base 10000 + Victim - (Attacker / 10).
        // Dividing Attacker by 10 ensures the score stays positive and valid.
        return 10000 + victimVal - (attackerVal / 10);
    }

    // B. PROMOTIONS (Always high priority)
    if (m.flag == MOVE_PROMOTION)
        return 9000;

    // C. KILLER MOVES
    if (ply >= 0 && ply < MAX_KILLER_PLY)
    {
        if (movesEqual(m, killerMoves[ply][0]))
            return 8500;
        if (movesEqual(m, killerMoves[ply][1]))
            return 8000;
    }

    // D. OTHER QUIET MOVES (History Heuristic)
    // Clamped below the killer-move band so a very frequently-cutting quiet
    // move can never be reordered ahead of an actual killer at this ply.
    int historyScore = historyTable[m.from.row * 8 + m.from.col][m.to.row * 8 + m.to.col];
    return (historyScore > 7000) ? 7000 : historyScore;
}

/**
 * @brief Sorts moves in descending order (best candidate first) using
 * Bubble Sort - see scoreMove() for the ranking.
 */
static void scoreMoves(BoardState *board, MoveList *list, int ply, Move ttMoveHint)
{
    int scores[MAX_MOVES_IN_LIST];
    // Pre-calculate scores
    for (int i = 0; i < list->count; i++)
        scores[i] = scoreMove(board, list->moves[i], ply, ttMoveHint);

    // Sort
    for (int i = 0; i < list->count - 1; i++)
    {
        for (int j = 0; j < list->count - i - 1; j++)
        {
            if (scores[j] < scores[j + 1])
            {
                int tempScore = scores[j];
                scores[j] = scores[j + 1];
                scores[j + 1] = tempScore;
                Move tempMove = list->moves[j];
                list->moves[j] = list->moves[j + 1];
                list->moves[j + 1] = tempMove;
            }
        }
    }
}

/* ========================================================================== */
/* 4. MOVE GENERATION (Standard Logic - Unchanged)                            */
/* ========================================================================== */

/**
 * @brief Generates all fully legal moves (checks included).
 */
MoveList generateAllLegalMoves(BoardState *board)
{
    MoveList pseudo;
    pseudo.count = 0;
    MoveList final;
    final.count = 0;

    generatePseudoLegalMoves(board, &pseudo);
    PieceColor currentPlayer = board->currentPlayer;

    for (int i = 0; i < pseudo.count; i++)
    {
        Move m = pseudo.moves[i];
        makeMove(board, m);
        // Filter: If King is in check, discard the move
        if (!isKingInCheck(board, currentPlayer))
            final.moves[final.count++] = m;
        undoMove(board, m);
    }
    return final;
}

// --- Dispatcher ---
static void generatePseudoLegalMoves(BoardState *board, MoveList *list)
{
    PieceColor player = board->currentPlayer;
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.color == player)
            {
                switch (p.type)
                {
                case PAWN:
                    generatePawnMoves(board, list, r, c);
                    break;
                case KNIGHT:
                    generateKnightMoves(board, list, r, c);
                    break;
                case BISHOP:
                case ROOK:
                case QUEEN:
                    generateSlidingMoves(board, list, r, c);
                    break;
                case KING:
                    generateKingMoves(board, list, r, c);
                    break;
                default:
                    break;
                }
            }
        }
    }
}

// --- Safe Adder ---
static void addMove(BoardState *board, MoveList *list, Move move)
{
    int toR = move.to.row;
    int toC = move.to.col;
    if (toR < 0 || toR >= 8 || toC < 0 || toC >= 8)
        return;
    if (move.flag != MOVE_EN_PASSANT && board->squares[toR][toC].color == board->currentPlayer)
        return;
    list->moves[list->count++] = move;
}

// --- Piece Generators ---

static void generatePawnMoves(BoardState *board, MoveList *list, int r, int c)
{
    PieceColor player = board->currentPlayer;
    int dir = (player == WHITE) ? -1 : 1;
    int startRow = (player == WHITE) ? 6 : 1;
    int promotionRank = (player == WHITE) ? 0 : 7;
    Position from = {r, c};

    // 1. Push
    if (r + dir >= 0 && r + dir < 8 && board->squares[r + dir][c].type == EMPTY)
    {
        if (r + dir == promotionRank)
        {
            addMove(board, list, (Move){from, {r + dir, c}, QUEEN, MOVE_PROMOTION});
            addMove(board, list, (Move){from, {r + dir, c}, ROOK, MOVE_PROMOTION});
            addMove(board, list, (Move){from, {r + dir, c}, BISHOP, MOVE_PROMOTION});
            addMove(board, list, (Move){from, {r + dir, c}, KNIGHT, MOVE_PROMOTION});
        }
        else
        {
            addMove(board, list, (Move){from, {r + dir, c}, EMPTY, MOVE_NORMAL});
        }
    }

    // 2. Double Push
    if (r == startRow && board->squares[r + dir][c].type == EMPTY && board->squares[r + 2 * dir][c].type == EMPTY)
    {
        addMove(board, list, (Move){from, {r + 2 * dir, c}, EMPTY, MOVE_NORMAL});
    }
    
    // 3. Captures
    int captureCols[] = {c - 1, c + 1};
    for (int i = 0; i < 2; i++)
    {
        int newC = captureCols[i];
        if (newC >= 0 && newC < 8)
        {
            Piece target = board->squares[r + dir][newC];
            // Normal Capture
            if (target.type != EMPTY && target.color != player)
            {
                if (r + dir == promotionRank)
                {
                    addMove(board, list, (Move){from, {r + dir, newC}, QUEEN, MOVE_PROMOTION});
                    addMove(board, list, (Move){from, {r + dir, newC}, ROOK, MOVE_PROMOTION});
                    addMove(board, list, (Move){from, {r + dir, newC}, BISHOP, MOVE_PROMOTION});
                    addMove(board, list, (Move){from, {r + dir, newC}, KNIGHT, MOVE_PROMOTION});
                }
                else
                {
                    addMove(board, list, (Move){from, {r + dir, newC}, EMPTY, MOVE_NORMAL});
                }
            }
            // En Passant
            if (r + dir == board->enPassantTarget.row && newC == board->enPassantTarget.col)
            {
                addMove(board, list, (Move){from, {r + dir, newC}, EMPTY, MOVE_EN_PASSANT});
            }
        }
    }
}

static void generateKnightMoves(BoardState *board, MoveList *list, int r, int c)
{
    int dR[] = {-2, -2, -1, -1, 1, 1, 2, 2};
    int dC[] = {-1, 1, -2, 2, -2, 2, -1, 1};
    for (int i = 0; i < 8; i++)
        addMove(board, list, (Move){{r, c}, {r + dR[i], c + dC[i]}, EMPTY, MOVE_NORMAL});
}

static void generateKingMoves(BoardState *board, MoveList *list, int r, int c)
{
    PieceColor player = board->currentPlayer;
    PieceColor opponent = (player == WHITE) ? BLACK : WHITE;
    int dR[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    int dC[] = {-1, 0, 1, -1, 1, -1, 0, 1};

    // Normal moves
    for (int i = 0; i < 8; i++)
        addMove(board, list, (Move){{r, c}, {r + dR[i], c + dC[i]}, EMPTY, MOVE_NORMAL});

    // Castling
    if (isKingInCheck(board, player))
        return;

    if (player == WHITE)
    {
        if (board->castling.wk && board->squares[7][5].type == EMPTY && board->squares[7][6].type == EMPTY &&
            !isSquareAttacked(board, 7, 5, opponent) && !isSquareAttacked(board, 7, 6, opponent))
            addMove(board, list, (Move){{7, 4}, {7, 6}, EMPTY, MOVE_CASTLE_KING});
        if (board->castling.wq && board->squares[7][1].type == EMPTY && board->squares[7][2].type == EMPTY &&
            board->squares[7][3].type == EMPTY && !isSquareAttacked(board, 7, 2, opponent) && !isSquareAttacked(board, 7, 3, opponent))
            addMove(board, list, (Move){{7, 4}, {7, 2}, EMPTY, MOVE_CASTLE_QUEEN});
    }
    else
    {
        if (board->castling.bk && board->squares[0][5].type == EMPTY && board->squares[0][6].type == EMPTY &&
            !isSquareAttacked(board, 0, 5, opponent) && !isSquareAttacked(board, 0, 6, opponent))
            addMove(board, list, (Move){{0, 4}, {0, 6}, EMPTY, MOVE_CASTLE_KING});
        if (board->castling.bq && board->squares[0][1].type == EMPTY && board->squares[0][2].type == EMPTY &&
            board->squares[0][3].type == EMPTY && !isSquareAttacked(board, 0, 2, opponent) && !isSquareAttacked(board, 0, 3, opponent))
            addMove(board, list, (Move){{0, 4}, {0, 2}, EMPTY, MOVE_CASTLE_QUEEN});
    }
}

static void generateSlidingMoves(BoardState *board, MoveList *list, int r, int c)
{
    Piece p = board->squares[r][c];
    int dR[] = {-1, -1, 1, 1, -1, 1, 0, 0};
    int dC[] = {-1, 1, -1, 1, 0, 0, -1, 1};
    int startDir = (p.type == BISHOP) ? 0 : (p.type == ROOK) ? 4
                                                             : 0;
    int endDir = (p.type == BISHOP) ? 4 : (p.type == ROOK) ? 8
                                                           : 8;

    for (int i = startDir; i < endDir; i++)
    {
        for (int k = 1; k < 8; k++)
        {
            int newR = r + dR[i] * k;
            int newC = c + dC[i] * k;
            if (newR < 0 || newR >= 8 || newC < 0 || newC >= 8)
                break;
            Piece target = board->squares[newR][newC];
            if (target.type == EMPTY)
            {
                addMove(board, list, (Move){{r, c}, {newR, newC}, EMPTY, MOVE_NORMAL});
            }
            else if (target.color != board->currentPlayer)
            {
                addMove(board, list, (Move){{r, c}, {newR, newC}, EMPTY, MOVE_NORMAL});
                break;
            }
            else
            {
                break;
            }
        }
    }
}

// --- Insufficient Material (Draw) ---
bool isInsufficientMaterial(BoardState *board)
{
    int minorPieceCount = 0;

    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            PieceType type = board->squares[r][c].type;

            // If there is a Pawn, Rook, or Queen, checkmate is definitely possible.
            if (type == PAWN || type == ROOK || type == QUEEN)
                return false;

            // Count Bishops and Knights
            if (type == BISHOP || type == KNIGHT)
                minorPieceCount++;
        }
    }

    // Insufficient Material Scenarios:
    // 0 Minors: King vs King
    // 1 Minor:  King + Knight vs King  OR  King + Bishop vs King
    if (minorPieceCount <= 1)
        return true;

    // If there are 2 or more minor pieces (e.g., 2 Bishops, or 1 Knight each),
    // a mate is theoretically possible (or at least not strictly impossible by rule).
    return false;
}