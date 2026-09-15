#include "eval.h"
#include "structs.h"
#include <stdbool.h>

/* * ============================================================================
 * TAPERED EVALUATION IMPLEMENTATION
 * ============================================================================
 * We calculate two scores: Middlegame (MG) and Endgame (EG).
 * We interpolate between them based on the "Game Phase".
 * * Game Phase Calculation:
 * We start with a max "Phase Value" (e.g., 24).
 * As pieces are captured, the phase value drops.
 * * Weights:
 * Knight/Bishop = 1
 * Rook = 2
 * Queen = 4
 * Total Start = 4*1 + 4*1 + 4*2 + 2*4 = 24
 */

#define PHASE_TOTAL 24

// --- Material Values (MG vs EG) ---
// Note: Pawns gain value in endgame (promotion potential)
static const int mg_value[] = {0, 82, 337, 365, 477, 1025, 0};
static const int eg_value[] = {0, 94, 281, 297, 512, 936, 0};

// --- Mobility Bonus ---
#define MOBILITY_MG 1
#define MOBILITY_EG 1 // Mobility is slightly less crucial in pure K+P endings, but good for pieces.

// ============================================================================
// PIECE-SQUARE TABLES (MG and EG)
// ============================================================================

// --- PAWN ---
// MG: Maintain structure, control center.
// EG: Push for promotion (Rank 7 is huge).
static const int pawn_mg[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {50, 50, 50, 50, 50, 50, 50, 50},
    {10, 10, 20, 30, 30, 20, 10, 10},
    {5, 5, 10, 25, 25, 10, 5, 5},
    {0, 0, 0, 20, 20, 0, 0, 0},
    {5, -5, -10, 0, 0, -10, -5, 5},
    {5, 10, 10, -20, -20, 10, 10, 5},
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const int pawn_eg[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {80, 80, 80, 80, 80, 80, 80, 80}, // Push!
    {50, 50, 50, 50, 50, 50, 50, 50},
    {30, 30, 30, 30, 30, 30, 30, 30},
    {20, 20, 20, 20, 20, 20, 20, 20},
    {10, 10, 10, 10, 10, 10, 10, 10},
    {10, 10, 10, 10, 10, 10, 10, 10},
    {0, 0, 0, 0, 0, 0, 0, 0}};

// --- KNIGHT ---
// MG: Stay central, avoid edges.
// EG: Similar, but penalties for edges are less severe as boards open up.
static const int knight_mg[8][8] = {
    {-50, -40, -30, -30, -30, -30, -40, -50},
    {-40, -20, 0, 0, 0, 0, -20, -40},
    {-30, 0, 10, 15, 15, 10, 0, -30},
    {-30, 5, 15, 20, 20, 15, 5, -30},
    {-30, 0, 15, 20, 20, 15, 0, -30},
    {-30, 5, 10, 15, 15, 10, 5, -30},
    {-40, -20, 0, 5, 5, 0, -20, -40},
    {-50, -10, -30, -30, -30, -30, -10, -50}};

static const int knight_eg[8][8] = {
    {-50, -40, -30, -30, -30, -30, -40, -50},
    {-40, -20, 0, 0, 0, 0, -20, -40},
    {-30, 0, 10, 15, 15, 10, 0, -30},
    {-30, 5, 15, 20, 20, 15, 5, -30},
    {-30, 0, 15, 20, 20, 15, 0, -30},
    {-30, 5, 10, 15, 15, 10, 5, -30},
    {-40, -20, 0, 5, 5, 0, -20, -40},
    {-50, -30, -20, -20, -20, -20, -30, -50}};

// --- BISHOP ---
// MG: Avoid edges, aim at king.
// EG: Centralize to control both sides of the board.
static const int bishop_mg[8][8] = {
    {-20, -10, -10, -10, -10, -10, -10, -20},
    {-10, 0, 0, 0, 0, 0, 0, -10},
    {-10, 0, 5, 10, 10, 5, 0, -10},
    {-10, 5, 5, 10, 10, 5, 5, -10},
    {-10, 0, 10, 10, 10, 10, 0, -10},
    {-10, 10, 10, 10, 10, 10, 10, -10},
    {-10, 5, 0, 0, 0, 0, 5, -10},
    {-20, -10, -10, -10, -10, -10, -10, -20}};

static const int bishop_eg[8][8] = {
    {-20, -10, -10, -10, -10, -10, -10, -20},
    {-10, 0, 0, 0, 0, 0, 0, -10},
    {-10, 0, 5, 10, 10, 5, 0, -10},
    {-10, 5, 5, 10, 10, 5, 5, -10},
    {-10, 0, 10, 10, 10, 10, 0, -10},
    {-10, 10, 10, 10, 10, 10, 10, -10},
    {-10, 5, 0, 0, 0, 0, 5, -10},
    {-20, -10, -10, -10, -10, -10, -10, -20}};

// --- ROOK ---
// MG: Open files, 7th rank, corners (castling).
// EG: 7th rank is crucial, active king support.
static const int rook_mg[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {5, 10, 10, 10, 10, 10, 10, 5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {0, -5, 0, 5, 5, 0, -5, 0}};

static const int rook_eg[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {10, 10, 10, 10, 10, 10, 10, 10},
    {5, 5, 5, 5, 5, 5, 5, 5},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0}};

// --- QUEEN ---
// MG: Stay safe, don't block knights.
// EG: Centralize, dominate.
static const int queen_mg[8][8] = {
    {-20, -10, -10, -5, -5, -10, -10, -20},
    {-10, 0, 0, 0, 0, 0, 0, -10},
    {-10, 0, 5, 5, 5, 5, 0, -10},
    {-5, 0, 5, 5, 5, 5, 0, -5},
    {0, 0, 5, 5, 5, 5, 0, -5},
    {-10, 0, 5, 5, 5, 5, 0, -10},
    {-10, 0, 5, 0, 0, 0, 0, -10},
    {-20, -10, -10, -5, -5, -10, -10, -20}};

static const int queen_eg[8][8] = {
    {-20, -10, -10, -5, -5, -10, -10, -20},
    {-10, 0, 0, 0, 0, 0, 0, -10},
    {-10, 0, 5, 5, 5, 5, 0, -10},
    {-5, 0, 5, 5, 5, 5, 0, -5},
    {0, 0, 5, 5, 5, 5, 0, -5},
    {-10, 0, 5, 5, 5, 5, 0, -10},
    {-10, 0, 5, 0, 0, 0, 0, -10},
    {-20, -10, -10, -5, -5, -10, -10, -20}};

// --- KING ---
// MG: Safety is priority. Corners +20/30. Center -50.
// EG: Activity is priority. Center +30. Corners -30.
static const int king_mg[8][8] = {
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-20, -30, -30, -40, -40, -30, -30, -20},
    {-10, -20, -20, -20, -20, -20, -20, -10},
    {20, 20, 0, 0, 0, 0, 20, 20},
    {20, 30, 10, 0, 0, 10, 30, 20}};

static const int king_eg[8][8] = {
    {-50, -40, -30, -20, -20, -30, -40, -50},
    {-30, -20, -10, 0, 0, -10, -20, -30},
    {-30, -10, 20, 30, 30, 20, -10, -30}, // Center is Great!
    {-30, -10, 30, 40, 40, 30, -10, -30},
    {-30, -10, 30, 40, 40, 30, -10, -30},
    {-30, -10, 20, 30, 30, 20, -10, -30},
    {-30, -30, 0, 0, 0, 0, -30, -30},
    {-50, -30, -30, -30, -30, -30, -30, -50} // Back rank is bad in endgame
};

// --- Helpers ---

// Retrieve table value based on piece type, phase (mg/eg), and color
static int getTableScore(const int table[8][8], int r, int c, PieceColor color)
{
    int row = (color == WHITE) ? r : 7 - r;
    int col = c;
    return table[row][col];
}

// --- Mobility Logic ---
static int countSlidingMoves(BoardState *board, int r, int c, Piece p)
{
    int count = 0;
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
            if (board->squares[newR][newC].type == EMPTY)
                count++;
            else if (board->squares[newR][newC].color != p.color)
            {
                count++;
                break;
            }
            else
                break;
        }
    }
    return count;
}

static int countKnightMoves(BoardState *board, int r, int c, Piece p)
{
    int count = 0;
    int dR[] = {-2, -2, -1, -1, 1, 1, 2, 2};
    int dC[] = {-1, 1, -2, 2, -2, 2, -1, 1};
    for (int i = 0; i < 8; i++)
    {
        int newR = r + dR[i];
        int newC = c + dC[i];
        if (newR >= 0 && newR < 8 && newC >= 0 && newC < 8)
        {
            if (board->squares[newR][newC].color != p.color)
                count++;
        }
    }
    return count;
}

// --- Game Phase ---

/**
 * @brief Estimates how far the game has progressed from opening (24) toward
 * a bare endgame (0), counting only knights/bishops/rooks/queens still on
 * the board (weighted the same as the tapered-eval phase blend below).
 * Exposed via eval.h so callers other than evaluateBoard (e.g. the AI's
 * time manager) can use the same "how much material/complexity is left"
 * signal without duplicating this logic.
 */
int getGamePhase(BoardState *board)
{
    int gamePhase = 0;
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            switch (board->squares[r][c].type)
            {
            case KNIGHT:
            case BISHOP:
                gamePhase += 1;
                break;
            case ROOK:
                gamePhase += 2;
                break;
            case QUEEN:
                gamePhase += 4;
                break;
            default:
                break;
            }
        }
    }
    if (gamePhase > PHASE_TOTAL)
        gamePhase = PHASE_TOTAL;
    return gamePhase;
}

// --- Structural Bonuses ---
// Beyond material/PST/mobility: pawn structure, bishop pair, rook files,
// and a simple king-safety term. See docs/SEARCH_AND_EVAL.md for the
// reasoning behind each. All of these are symmetric by construction (the
// same function is called for both colors and the results subtracted), so
// they contribute exactly 0 to the standard starting position, same as
// every other term here.

#define BISHOP_PAIR_MG 30
#define BISHOP_PAIR_EG 40

#define ROOK_OPEN_FILE_MG 20
#define ROOK_OPEN_FILE_EG 15
#define ROOK_SEMIOPEN_FILE_MG 10
#define ROOK_SEMIOPEN_FILE_EG 8

#define DOUBLED_PAWN_PENALTY_MG 10
#define DOUBLED_PAWN_PENALTY_EG 20

#define ISOLATED_PAWN_PENALTY_MG 12
#define ISOLATED_PAWN_PENALTY_EG 18

/* Only added to the middlegame score, not the endgame one - king safety
 * matters far less once the board has simplified, and the tapered blend
 * already suppresses mg-only terms automatically as the phase drops. */
#define KING_SHIELD_BONUS_MG 10

/* Indexed by "ranks advanced from this pawn's own starting rank" (0..6);
 * bigger in the endgame, where a passed pawn's promotion threat is much
 * more dangerous with fewer pieces around to stop it. */
static const int passedPawnBonusMg[8] = {0, 5, 10, 20, 35, 60, 100, 0};
static const int passedPawnBonusEg[8] = {0, 10, 20, 40, 70, 120, 200, 0};

static void countPawnFiles(BoardState *board, PieceColor color, int fileCounts[8])
{
    for (int i = 0; i < 8; i++)
        fileCounts[i] = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.type == PAWN && p.color == color)
                fileCounts[c]++;
        }
}

/* A pawn is passed if no enemy pawn on its own file or either adjacent
 * file is still ahead of it (between it and the promotion square). */
static bool pawnIsPassed(BoardState *board, PieceColor color, int r, int c)
{
    PieceColor enemy = (color == WHITE) ? BLACK : WHITE;
    int dir = (color == WHITE) ? -1 : 1; // toward promotion
    for (int cc = c - 1; cc <= c + 1; cc++)
    {
        if (cc < 0 || cc > 7)
            continue;
        for (int rr = r + dir; rr >= 0 && rr <= 7; rr += dir)
        {
            Piece p = board->squares[rr][cc];
            if (p.type == PAWN && p.color == enemy)
                return false;
        }
    }
    return true;
}

/* Doubled and isolated pawns are structural weaknesses (penalty); passed
 * pawns are a structural strength (bonus), scaled by how far advanced. */
static void pawnStructureScore(BoardState *board, PieceColor color, const int fileCounts[8], int *mg, int *eg)
{
    for (int f = 0; f < 8; f++)
    {
        if (fileCounts[f] > 1)
        {
            int extraPawns = fileCounts[f] - 1;
            *mg -= extraPawns * DOUBLED_PAWN_PENALTY_MG;
            *eg -= extraPawns * DOUBLED_PAWN_PENALTY_EG;
        }
    }

    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.type != PAWN || p.color != color)
                continue;

            bool leftHasPawn = (c > 0) && fileCounts[c - 1] > 0;
            bool rightHasPawn = (c < 7) && fileCounts[c + 1] > 0;
            if (!leftHasPawn && !rightHasPawn)
            {
                *mg -= ISOLATED_PAWN_PENALTY_MG;
                *eg -= ISOLATED_PAWN_PENALTY_EG;
            }

            if (pawnIsPassed(board, color, r, c))
            {
                int advancement = (color == WHITE) ? (6 - r) : (r - 1);
                if (advancement < 0)
                    advancement = 0;
                if (advancement > 7)
                    advancement = 7;
                *mg += passedPawnBonusMg[advancement];
                *eg += passedPawnBonusEg[advancement];
            }
        }
    }
}

static int countBishops(BoardState *board, PieceColor color)
{
    int count = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (board->squares[r][c].type == BISHOP && board->squares[r][c].color == color)
                count++;
    return count;
}

static void rookFileScore(BoardState *board, PieceColor color, const int ownPawnFiles[8],
                           const int enemyPawnFiles[8], int *mg, int *eg)
{
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.type != ROOK || p.color != color)
                continue;

            bool ownPawnOnFile = ownPawnFiles[c] > 0;
            bool enemyPawnOnFile = enemyPawnFiles[c] > 0;
            if (!ownPawnOnFile && !enemyPawnOnFile)
            {
                *mg += ROOK_OPEN_FILE_MG;
                *eg += ROOK_OPEN_FILE_EG;
            }
            else if (!ownPawnOnFile)
            {
                *mg += ROOK_SEMIOPEN_FILE_MG;
                *eg += ROOK_SEMIOPEN_FILE_EG;
            }
        }
    }
}

static Position findKingSquare(BoardState *board, PieceColor color)
{
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (board->squares[r][c].type == KING && board->squares[r][c].color == color)
                return (Position){r, c};
    return (Position){-1, -1};
}

/* Counts friendly pawns on the rank directly in front of the king, across
 * its own file and the two adjacent ones - a simple, standard proxy for
 * king safety (an intact pawn shield vs. an exposed king). */
static int kingShieldScore(BoardState *board, PieceColor color)
{
    Position king = findKingSquare(board, color);
    if (king.row == -1)
        return 0;

    int dir = (color == WHITE) ? -1 : 1;
    int shieldRank = king.row + dir;
    if (shieldRank < 0 || shieldRank > 7)
        return 0;

    int shieldCount = 0;
    for (int c = king.col - 1; c <= king.col + 1; c++)
    {
        if (c < 0 || c > 7)
            continue;
        Piece p = board->squares[shieldRank][c];
        if (p.type == PAWN && p.color == color)
            shieldCount++;
    }
    return shieldCount * KING_SHIELD_BONUS_MG;
}

// --- Main Evaluation ---

int evaluateBoard(BoardState *board)
{
    int mgScore = 0;
    int egScore = 0;

    // 1. Iterate Board
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.type != EMPTY)
            {
                // A. Calculate Material & Mobility
                int m_val = 0, e_val = 0;
                m_val = mg_value[p.type];
                e_val = eg_value[p.type];

                // Positional Scores (PST)
                switch (p.type)
                {
                case PAWN:
                    m_val += getTableScore(pawn_mg, r, c, p.color);
                    e_val += getTableScore(pawn_eg, r, c, p.color);
                    break;
                case KNIGHT:
                    m_val += getTableScore(knight_mg, r, c, p.color);
                    e_val += getTableScore(knight_eg, r, c, p.color);
                    m_val += countKnightMoves(board, r, c, p) * MOBILITY_MG;
                    e_val += countKnightMoves(board, r, c, p) * MOBILITY_EG;
                    break;
                case BISHOP:
                    m_val += getTableScore(bishop_mg, r, c, p.color);
                    e_val += getTableScore(bishop_eg, r, c, p.color);
                    m_val += countSlidingMoves(board, r, c, p) * MOBILITY_MG;
                    e_val += countSlidingMoves(board, r, c, p) * MOBILITY_EG;
                    break;
                case ROOK:
                    m_val += getTableScore(rook_mg, r, c, p.color);
                    e_val += getTableScore(rook_eg, r, c, p.color);
                    m_val += countSlidingMoves(board, r, c, p) * MOBILITY_MG;
                    e_val += countSlidingMoves(board, r, c, p) * MOBILITY_EG;
                    break;
                case QUEEN:
                    m_val += getTableScore(queen_mg, r, c, p.color);
                    e_val += getTableScore(queen_eg, r, c, p.color);
                    m_val += countSlidingMoves(board, r, c, p) * MOBILITY_MG;
                    e_val += countSlidingMoves(board, r, c, p) * MOBILITY_EG;
                    break;
                case KING:
                    m_val += getTableScore(king_mg, r, c, p.color);
                    e_val += getTableScore(king_eg, r, c, p.color);
                    break;
                default:
                    break;
                }

                // C. Add to Totals
                if (p.color == WHITE)
                {
                    mgScore += m_val;
                    egScore += e_val;
                }
                else
                {
                    mgScore -= m_val;
                    egScore -= e_val;
                }
            }
        }
    }

    // 1b. Structural bonuses: pawn structure, bishop pair, rook files, king safety.
    int whiteMg = 0, whiteEg = 0, blackMg = 0, blackEg = 0;

    int whitePawnFiles[8], blackPawnFiles[8];
    countPawnFiles(board, WHITE, whitePawnFiles);
    countPawnFiles(board, BLACK, blackPawnFiles);

    pawnStructureScore(board, WHITE, whitePawnFiles, &whiteMg, &whiteEg);
    pawnStructureScore(board, BLACK, blackPawnFiles, &blackMg, &blackEg);

    rookFileScore(board, WHITE, whitePawnFiles, blackPawnFiles, &whiteMg, &whiteEg);
    rookFileScore(board, BLACK, blackPawnFiles, whitePawnFiles, &blackMg, &blackEg);

    if (countBishops(board, WHITE) >= 2)
    {
        whiteMg += BISHOP_PAIR_MG;
        whiteEg += BISHOP_PAIR_EG;
    }
    if (countBishops(board, BLACK) >= 2)
    {
        blackMg += BISHOP_PAIR_MG;
        blackEg += BISHOP_PAIR_EG;
    }

    whiteMg += kingShieldScore(board, WHITE);
    blackMg += kingShieldScore(board, BLACK);

    mgScore += whiteMg - blackMg;
    egScore += whiteEg - blackEg;

    // 2. Tapered Evaluation Formula
    int mgWeight = getGamePhase(board);
    int egWeight = PHASE_TOTAL - mgWeight;

    // Final Score = (MG_Score * Phase + EG_Score * (24 - Phase)) / 24
    return ((mgScore * mgWeight) + (egScore * egWeight)) / PHASE_TOTAL;
}