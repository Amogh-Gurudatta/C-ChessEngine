#include "eval.h"
#include "structs.h" // From Person 1

// --- Material Values ---
// These are standard piece values (in "centipawns")
#define PAWN_VALUE   100
#define KNIGHT_VALUE 320
#define BISHOP_VALUE 330
#define ROOK_VALUE   500
#define QUEEN_VALUE  900
#define KING_VALUE   20000 // A very large value

// --- Piece-Square Tables (Positional Factors) ---
// These tables give bonuses/penalties for a piece being on a specific square.
// All tables are defined from WHITE's perspective.
// Black's score will be read by flipping the row index (e.g., row 0 becomes row 7).

static const int pawnTable[8][8] = {
    {0,  0,  0,  0,  0,  0,  0,  0},
    {50, 50, 50, 50, 50, 50, 50, 50}, // Pawns on 7th rank are strong
    {10, 10, 20, 30, 30, 20, 10, 10},
    { 5,  5, 10, 25, 25, 10,  5,  5},
    { 0,  0,  0, 20, 20,  0,  0,  0},
    { 5, -5,-10,  0,  0,-10, -5,  5},
    { 5, 10, 10,-20,-20, 10, 10,  5},
    {0,  0,  0,  0,  0,  0,  0,  0}
};

static const int knightTable[8][8] = {
    {-50,-40,-30,-30,-30,-30,-40,-50},
    {-40,-20,  0,  0,  0,  0,-20,-40},
    {-30,  0, 10, 15, 15, 10,  0,-30},
    {-30,  5, 15, 20, 20, 15,  5,-30}, // Knights are strong in the center
    {-30,  0, 15, 20, 20, 15,  0,-30},
    {-30,  5, 10, 15, 15, 10,  5,-30},
    {-40,-20,  0,  5,  5,  0,-20,-40},
    {-50,-40,-30,-30,-30,-30,-40,-50}
};

static const int bishopTable[8][8] = {
    {-20,-10,-10,-10,-10,-10,-10,-20},
    {-10,  0,  0,  0,  0,  0,  0,-10},
    {-10,  0,  5, 10, 10,  5,  0,-10},
    {-10,  5,  5, 10, 10,  5,  5,-10},
    {-10,  0, 10, 10, 10, 10,  0,-10},
    {-10, 10, 10, 10, 10, 10, 10,-10},
    {-10,  5,  0,  0,  0,  0,  5,-10},
    {-20,-10,-10,-10,-10,-10,-10,-20}
};

static const int rookTable[8][8] = {
    { 0,  0,  0,  0,  0,  0,  0,  0},
    { 5, 10, 10, 10, 10, 10, 10,  5}, // Rooks love open ranks
    {-5,  0,  0,  0,  0,  0,  0, -5},
    {-5,  0,  0,  0,  0,  0,  0, -5},
    {-5,  0,  0,  0,  0,  0,  0, -5},
    {-5,  0,  0,  0,  0,  0,  0, -5},
    {-5,  0,  0,  0,  0,  0,  0, -5},
    { 0,  0,  0,  5,  5,  0,  0,  0}
};

static const int queenTable[8][8] = {
    {-20,-10,-10, -5, -5,-10,-10,-20},
    {-10,  0,  0,  0,  0,  0,  0,-10},
    {-10,  0,  5,  5,  5,  5,  0,-10},
    { -5,  0,  5,  5,  5,  5,  0, -5},
    {  0,  0,  5,  5,  5,  5,  0, -5},
    {-10,  5,  5,  5,  5,  5,  0,-10},
    {-10,  0,  5,  0,  0,  0,  0,-10},
    {-20,-10,-10, -5, -5,-10,-10,-20}
};

// King safety table (Middlegame)
static const int kingTable[8][8] = {
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-20,-30,-30,-40,-40,-30,-30,-20},
    {-10,-20,-20,-20,-20,-20,-20,-10},
    { 20, 20,  0,  0,  0,  0, 20, 20}, // Good to be castled
    { 20, 30, 10,  0,  0, 10, 30, 20}
};

// --- Private Helper Functions ---

/**
 * @brief Gets the base material value for a piece.
 */
static int getPieceValue(PieceType type) {
    switch (type) {
        case PAWN:   return PAWN_VALUE;
        case KNIGHT: return KNIGHT_VALUE;
        case BISHOP: return BISHOP_VALUE;
        case ROOK:   return ROOK_VALUE;
        case QUEEN:  return QUEEN_VALUE;
        case KING:   return KING_VALUE;
        default:     return 0;
    }
}

/**
 * @brief Gets the positional score for a piece on a specific square.
 * This function correctly flips the board for Black.
 */
static int getPiecePositionalScore(PieceType type, PieceColor color, int r, int c) {
    // Select the correct table and flip the row 'r' if the piece is Black
    int row = (color == WHITE) ? r : 7 - r;
    int col = c;

    switch (type) {
        case PAWN:   return pawnTable[row][col];
        case KNIGHT: return knightTable[row][col];
        case BISHOP: return bishopTable[row][col];
        case ROOK:   return rookTable[row][col];
        case QUEEN:  return queenTable[row][col];
        case KING:   return kingTable[row][col];
        default:     return 0;
    }
}

// --- Public Function (from eval.h) ---

/**
 * @brief Evaluates the current board state and returns a static score.
 * This function iterates through all 64 squares, summing up the
 * material and positional scores for every piece.
 */
int evaluateBoard(BoardState *board) {
    int totalScore = 0;

    // Loop over the entire 8x8 board
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            Piece p = board->squares[r][c];

            if (p.type != EMPTY) {
                // Get the material and positional score for the piece
                int materialScore = getPieceValue(p.type);
                int positionalScore = getPiecePositionalScore(p.type, p.color, r, c);

                if (p.color == WHITE) {
                    // Add score for White pieces
                    totalScore += materialScore + positionalScore;
                } else {
                    // Subtract score for Black pieces
                    totalScore -= (materialScore + positionalScore);
                }
            }
        }
    }

    // Return the final score from White's perspective
    return totalScore;
}