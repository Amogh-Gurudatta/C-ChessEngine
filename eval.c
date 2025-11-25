#include "eval.h"
#include "structs.h"

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

// --- Main Evaluation ---

int evaluateBoard(BoardState *board)
{
    int mgScore = 0;
    int egScore = 0;
    int gamePhase = 0;

    // 1. Iterate Board
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.type != EMPTY)
            {
                // A. Update Game Phase
                // We only count major pieces for phase calculation
                switch (p.type)
                {
                case KNIGHT:
                    gamePhase += 1;
                    break;
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

                // B. Calculate Material & Mobility
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

    // 2. Tapered Evaluation Formula
    // Cap gamePhase at 24
    if (gamePhase > PHASE_TOTAL)
        gamePhase = PHASE_TOTAL;

    int mgWeight = gamePhase;
    int egWeight = PHASE_TOTAL - mgWeight;

    // Final Score = (MG_Score * Phase + EG_Score * (24 - Phase)) / 24
    return ((mgScore * mgWeight) + (egScore * egWeight)) / PHASE_TOTAL;
}