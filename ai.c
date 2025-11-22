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
#include <limits.h>

#include "ai.h"
#include "eval.h"
#include "game.h"
#include "structs.h"

/* * SEARCH_DEPTH: The fixed number of half-moves (plies) the engine searches.
 * Depth 4 allows the engine to see 2 full moves ahead for both sides.
 */
#define SEARCH_DEPTH 6
#define INFINITY_SCORE 1000000
#define MATE_VALUE (INFINITY_SCORE - 1000)

/* -------------------------------------------------------------------------- */
/* INTERNAL FUNCTION PROTOTYPES                        */
/* -------------------------------------------------------------------------- */

/* Core Search Logic */
static int negamax(BoardState *board, int depth, int alpha, int beta, int ply);
static int quiescence(BoardState *board, int alpha, int beta);

/* Heuristics & Ordering */
static int scoreMove(BoardState *board, Move m);
static void scoreMoves(BoardState *board, MoveList *list);

/* Move Generation Helpers (Standard Chess Logic) */
static void generatePseudoLegalMoves(BoardState *board, MoveList *list);
static void generatePawnMoves(BoardState *board, MoveList *list, int r, int c);
static void generateKnightMoves(BoardState *board, MoveList *list, int r, int c);
static void generateKingMoves(BoardState *board, MoveList *list, int r, int c);
static void generateSlidingMoves(BoardState *board, MoveList *list, int r, int c);
static void addMove(BoardState *board, MoveList *list, Move move);
static bool isInsufficientMaterial(BoardState *board);

/* ========================================================================== */
/* 1. ROOT MOVE SEARCH (Entry Point)                                          */
/* ========================================================================== */

/**
 * @brief Calculates the best move for the current player using NegaMax.
 * This function is the "Root" of the search tree.
 * * @param board The current state of the game board.
 * @return The optimal Move found.
 */
Move findBestMove(BoardState *board)
{
    Move bestMove;
    bestMove.from = (Position){-1, -1}; // Initialize to invalid to detect errors

    // Initialize Alpha (Lower Bound) and Beta (Upper Bound)
    int alpha = -INFINITY_SCORE;
    int beta = INFINITY_SCORE;
    int bestVal = -INFINITY_SCORE;

    // 1. Generate all legal moves
    MoveList legalMoves = generateAllLegalMoves(board);

    // 2. Sort moves: Check Captures first!
    // Finding a good move early allows Alpha-Beta to prune bad branches later.
    scoreMoves(board, &legalMoves);

    // 3. Iterate through all root moves
    for (int i = 0; i < legalMoves.count; i++)
    {
        Move currentMove = legalMoves.moves[i];

        makeMove(board, currentMove);

        /* * RECURSIVE CALL (NegaMax Variant):
         * value = -negamax(...)
         * We flip the result because the opponent's score is bad for us.
         * We swap -beta and -alpha to reflect the perspective shift.
         */
        int val = -negamax(board, SEARCH_DEPTH - 1, -beta, -alpha, 1);

        undoMove(board, currentMove);

        // Update best move found so far
        if (val > bestVal)
        {
            bestVal = val;
            bestMove = currentMove;
        }

        // Update Alpha (The best score we can guarantee)
        if (val > alpha)
        {
            alpha = val;
        }
    }

    // Fail-safe: If no moves improved -INFINITY (rare/bug), pick the first legal move.
    if (bestMove.from.row == -1 && legalMoves.count > 0)
    {
        bestMove = legalMoves.moves[0];
    }

    return bestMove;
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
    MoveList moves = generateAllLegalMoves(board);
    scoreMoves(board, &moves);

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
 * * @param depth Remaining depth to search.
 * @param alpha Best score maximizer can guarantee.
 * @param beta Best score minimizer can guarantee.
 * @return The evaluation score relative to the side to move.
 */
static int negamax(BoardState *board, int depth, int alpha, int beta, int ply)
{
    // BASE CASE 1: Draw Rules (50-move rule or Insufficient Material)
    if (board->halfmoveClock >= 100 || isInsufficientMaterial(board))
        return 0;

    // CHECK EXTENSION
    // If we are in check, we extend the search depth by 1.
    // This ensures we don't stop searching just before a checkmate.
    bool inCheck = isKingInCheck(board, board->currentPlayer);
    if (inCheck)
    {
        depth++;
    }

    // BASE CASE 2: Depth Limit Reached -> Enter Quiescence Search
    if (depth <= 0)
        return quiescence(board, alpha, beta);

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

    // Sort Moves (Captures first for pruning)
    scoreMoves(board, &legalMoves);

    // RECURSION
    int maxVal = -INFINITY_SCORE;

    for (int i = 0; i < legalMoves.count; i++)
    {
        makeMove(board, legalMoves.moves[i]);

        // NegaMax Step: Flip alpha/beta, negate result.
        int score = -negamax(board, depth - 1, -beta, -alpha, ply + 1);

        undoMove(board, legalMoves.moves[i]);

        // Track best score
        if (score > maxVal)
            maxVal = score;

        // Update Alpha
        if (score > alpha)
            alpha = score;

        // Beta Pruning: Opponent has a better option elsewhere.
        if (alpha >= beta)
            break;
    }

    return maxVal;
}

/* ========================================================================== */
/* 3. HEURISTICS & HELPERS (MVV-LVA)                                          */
/* ========================================================================== */

/**
 * @brief Assigns a score to a move for sorting purposes.
 * Uses MVV-LVA: Most Valuable Victim - Least Valuable Aggressor.
 * * @return Higher score = Better candidate to search first.
 */
static int scoreMove(BoardState *board, Move m)
{
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

    // C. QUIET MOVES (Future: History Heuristic)
    return 0;
}

/**
 * @brief Sorts moves in descending order using Bubble Sort.
 */
static void scoreMoves(BoardState *board, MoveList *list)
{
    int scores[MAX_MOVES_IN_LIST];
    // Pre-calculate scores
    for (int i = 0; i < list->count; i++)
        scores[i] = scoreMove(board, list->moves[i]);

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
static bool isInsufficientMaterial(BoardState *board)
{
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            PieceType type = board->squares[r][c].type;
            // If we find anything other than a King, game is not drawn yet.
            if (type != EMPTY && type != KING)
                return false;
        }
    }
    return true;
}