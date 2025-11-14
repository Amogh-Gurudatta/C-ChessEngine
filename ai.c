#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h> // For INT_MIN and INT_MAX

#include "ai.h"
#include "eval.h"     // From Person 3 (self) - for evaluateBoard()
#include "game.h"     // From Person 2 - for makeMove(), undoMove(), isKingInCheck(), isSquareAttacked()
#include "structs.h"  // From Person 1

// Define the search depth for the Minimax algorithm
#define SEARCH_DEPTH 3

// --- Private Function Declarations ---

/**
 * The core recursive Minimax function.
 */
static int minimax(BoardState *board, int depth, bool isMaximizingPlayer);

/**
 * Generates a list of all moves that are *fully legal*
 * (i.e., do not result in the player's own king being in check).
 */
static MoveList generateAllLegalMoves(BoardState *board);

/**
 * Generates a list of *pseudo-legal* moves (all possible piece moves,
 * ignoring whether they put the king in check).
 */
static void generatePseudoLegalMoves(BoardState *board, MoveList *list);

// Move Generation Helpers
static void generatePawnMoves(BoardState *board, MoveList *list, int r, int c);
static void generateKnightMoves(BoardState *board, MoveList *list, int r, int c);
static void generateKingMoves(BoardState *board, MoveList *list, int r, int c);
static void generateSlidingMoves(BoardState *board, MoveList *list, int r, int c);

/**
 * @brief Helper to add a move to the list with flags.
 */
static void addMove(BoardState *board, MoveList *list, Move move);

/**
 * @brief Helper to check if a square is attacked by the opponent.
 */
static bool isSquareAttacked(BoardState *board, int r, int c, PieceColor attackerColor);

/**
 * @brief Checks for insufficient material (e.g., King vs King).
 */
static bool isInsufficientMaterial(BoardState *board);


// --- Public Function (from ai.h) ---

Move findBestMove(BoardState *board) {
    Move bestMove;
    int bestValue = INT_MIN;
    bool foundMove = false;

    // Get all legal moves for the current player (the AI)
    MoveList legalMoves = generateAllLegalMoves(board);

    // Iterate through all legal moves
    for (int i = 0; i < legalMoves.count; i++) {
        Move currentMove = legalMoves.moves[i];

        // 1. Make the move (ASSUMPTION: Person 2's makeMove handles all flags)
        makeMove(board, currentMove);

        // 2. Call minimax for the *minimizing* player (opponent)
        int moveValue = minimax(board, SEARCH_DEPTH - 1, false);

        // 3. Undo the move (ASSUMPTION: Person 2's undoMove handles all flags)
        undoMove(board, currentMove);

        // Check if this move is better than the current best
        if (moveValue > bestValue) {
            bestValue = moveValue;
            bestMove = currentMove;
            foundMove = true;
        }
    }

    if (!foundMove) {
        // This handles stalemate or checkmate (no legal moves)
        // Return a "null" move. main.c should check for this.
        bestMove.from = (Position){-1, -1};
        bestMove.to = (Position){-1, -1};
        bestMove.flag = MOVE_NORMAL;
    }
    
    return bestMove;
}

// --- Minimax Core ---

/**
 * @brief The core recursive Minimax function.
 * @param board The current board state to evaluate.
 * @param depth The remaining search depth.
 * @param isMaximizingPlayer True if it's the AI's turn, false for the human.
 * @return The evaluation score for this board state.
 */
static int minimax(BoardState *board, int depth, bool isMaximizingPlayer) {
    
    // Base Case 1: If we've reached max depth, return the static evaluation
    if (depth == 0) {
        // (ASSUMPTION: Person 3's eval.c provides this function)
        return evaluateBoard(board);
    }

    // --- NEW DRAW CHECKS ---
    // Base Case 2: 50-Move Rule (100 half-moves)
    // ASSUMPTION: board->halfmoveClock is managed by Person 2's makeMove/undoMove
    if (board->halfmoveClock >= 100) {
        return 0; // Draw
    }
    
    // Base Case 3: Insufficient Material
    if (isInsufficientMaterial(board)) {
        return 0; // Draw
    }
    // --- END NEW DRAW CHECKS ---

    MoveList legalMoves = generateAllLegalMoves(board);

    // Base Case 4: Checkmate or Stalemate
    if (legalMoves.count == 0) {
        // (ASSUMPTION: Person 2's isKingInCheck() is available)
        if (isKingInCheck(board, board->currentPlayer)) {
            // Checkmate: This is very bad for the current player
            return isMaximizingPlayer ? INT_MIN : INT_MAX; 
        } else {
            // Stalemate: A draw
            return 0;
        }
    }

    if (isMaximizingPlayer) {
        // --- AI's Turn (Maximize) ---
        int bestValue = INT_MIN;
        for (int i = 0; i < legalMoves.count; i++) {
            makeMove(board, legalMoves.moves[i]);
            int value = minimax(board, depth - 1, false);
            bestValue = (value > bestValue) ? value : bestValue;
            undoMove(board, legalMoves.moves[i]);
        }
        return bestValue;
    } else {
        // --- Human's Turn (Minimize) ---
        int bestValue = INT_MAX;
        for (int i = 0; i < legalMoves.count; i++) {
            makeMove(board, legalMoves.moves[i]);
            int value = minimax(board, depth - 1, true);
            bestValue = (value < bestValue) ? value : bestValue;
            undoMove(board, legalMoves.moves[i]);
        }
        return bestValue;
    }
}

// --- HELPER FUNCTION ---

/**
 * @brief Checks for insufficient material.
 * This is a simplified version checking only for King vs. King.
 * A more advanced version would also check for K vs KN, K vs KB.
 */
static bool isInsufficientMaterial(BoardState *board) {
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            PieceType type = board->squares[r][c].type;
            if (type != EMPTY && type != KING) {
                // Found a piece that is not a King, so material is sufficient
                return false;
            }
        }
    }
    // If the loop completes, only Kings and Empty squares are on the board
    return true;
}


// --- Legal Move Generation ---

/**
 * @brief Generates all *fully legal* moves.
 * It does this by first generating all pseudo-legal moves,
 * then filtering out any that leave the king in check.
 */
static MoveList generateAllLegalMoves(BoardState *board) {
    MoveList pseudoLegalMoves;
    pseudoLegalMoves.count = 0;
    
    MoveList finalLegalMoves;
    finalLegalMoves.count = 0;

    // 1. Get all pseudo-legal moves
    generatePseudoLegalMoves(board, &pseudoLegalMoves);
    
    PieceColor currentPlayer = board->currentPlayer;

    // 2. Filter for legality (moves that don't put king in check)
    for (int i = 0; i < pseudoLegalMoves.count; i++) {
        Move move = pseudoLegalMoves.moves[i];
        
        // Simulate the move
        makeMove(board, move); // From game.c
        
        // Check if the current player's king is NOT in check
        if (!isKingInCheck(board, currentPlayer)) { // From game.c
            finalLegalMoves.moves[finalLegalMoves.count++] = move;
        }
        
        // Undo the move
        undoMove(board, move); // From game.c
    }
    
    return finalLegalMoves;
}

/**
 * @brief Fills a MoveList with all pseudo-legal moves.
 */
static void generatePseudoLegalMoves(BoardState *board, MoveList *list) {
    PieceColor player = board->currentPlayer;

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            Piece p = board->squares[r][c];
            if (p.color == player) {
                switch (p.type) {
                    case PAWN:   generatePawnMoves(board, list, r, c);   break;
                    case KNIGHT: generateKnightMoves(board, list, r, c); break;
                    case BISHOP: // Fall-through
                    case ROOK:   // Fall-through
                    case QUEEN:  generateSlidingMoves(board, list, r, c); break;
                    case KING:   generateKingMoves(board, list, r, c);   break;
                    default:     break;
                }
            }
        }
    }
}

/**
 * @brief Helper to add a move, handling validation and flags.
 * This checks boundaries and ensures we don't move onto our own piece.
 */
static void addMove(BoardState *board, MoveList *list, Move move) {
    int toR = move.to.row;
    int toC = move.to.col;

    // Ensure move is on the board
    if (toR < 0 || toR >= 8 || toC < 0 || toC >= 8) return;

    // Check if destination is occupied by our own piece
    // (Pawn captures are handled separately in their function)
    if (move.flag != MOVE_EN_PASSANT) {
        if (board->squares[toR][toC].color == board->currentPlayer) return;
    }

    list->moves[list->count++] = move;
}


// --- Piece-Specific Move Generation (Updated) ---

static void generatePawnMoves(BoardState *board, MoveList *list, int r, int c) {
    PieceColor player = board->currentPlayer;
    int dir = (player == WHITE) ? -1 : 1; // White moves up (row--), Black moves down (row++)
    int startRow = (player == WHITE) ? 6 : 1;
    int promotionRank = (player == WHITE) ? 0 : 7;

    Position from = {r, c};

    // --- 1. Single move forward ---
    if (r + dir >= 0 && r + dir < 8 && board->squares[r + dir][c].type == EMPTY) {
        if (r + dir == promotionRank) {
            // Handle Promotion: Add 4 moves, one for each promotion piece
            addMove(board, list, (Move){from, {r + dir, c}, QUEEN, MOVE_PROMOTION});
            addMove(board, list, (Move){from, {r + dir, c}, ROOK, MOVE_PROMOTION});
            addMove(board, list, (Move){from, {r + dir, c}, BISHOP, MOVE_PROMOTION});
            addMove(board, list, (Move){from, {r + dir, c}, KNIGHT, MOVE_PROMOTION});
        } else {
            // Normal 1-square push
            addMove(board, list, (Move){from, {r + dir, c}, EMPTY, MOVE_NORMAL});
        }
    }

    // --- 2. Double move from start ---
    if (r == startRow && board->squares[r + dir][c].type == EMPTY && board->squares[r + 2 * dir][c].type == EMPTY) {
        addMove(board, list, (Move){from, {r + 2 * dir, c}, EMPTY, MOVE_NORMAL});
    }

    // --- 3. Captures ---
    int captureCols[] = {c - 1, c + 1};
    for (int i = 0; i < 2; i++) {
        int newC = captureCols[i];
        if (newC >= 0 && newC < 8) {
            // 3a. Standard Capture
            Piece target = board->squares[r + dir][newC];
            if (target.type != EMPTY && target.color != player) {
                if (r + dir == promotionRank) {
                    // Capture with Promotion
                    addMove(board, list, (Move){from, {r + dir, newC}, QUEEN, MOVE_PROMOTION});
                    addMove(board, list, (Move){from, {r + dir, newC}, ROOK, MOVE_PROMOTION});
                    addMove(board, list, (Move){from, {r + dir, newC}, BISHOP, MOVE_PROMOTION});
                    addMove(board, list, (Move){from, {r + dir, newC}, KNIGHT, MOVE_PROMOTION});
                } else {
                    // Normal capture
                    addMove(board, list, (Move){from, {r + dir, newC}, EMPTY, MOVE_NORMAL});
                }
            }
            
            // 3b. En Passant Capture
            // ASSUMPTION: board->enPassantTarget is the *square behind* the pawn that just moved
            if (r + dir == board->enPassantTarget.row && newC == board->enPassantTarget.col) {
                 addMove(board, list, (Move){from, {r + dir, newC}, EMPTY, MOVE_EN_PASSANT});
            }
        }
    }
}

static void generateKnightMoves(BoardState *board, MoveList *list, int r, int c) {
    // 8 possible L-shaped moves
    int dR[] = {-2, -2, -1, -1,  1,  1,  2,  2};
    int dC[] = {-1,  1, -2,  2, -2,  2, -1,  1};

    for (int i = 0; i < 8; i++) {
        addMove(board, list, (Move){{r, c}, {r + dR[i], c + dC[i]}, EMPTY, MOVE_NORMAL});
    }
}

static void generateKingMoves(BoardState *board, MoveList *list, int r, int c) {
    PieceColor player = board->currentPlayer;
    PieceColor opponent = (player == WHITE) ? BLACK : WHITE;

    // --- 1. Standard 1-square moves ---
    // 8 directions for a king, 1 step
    int dR[] = {-1, -1, -1,  0,  0,  1,  1,  1};
    int dC[] = {-1,  0,  1, -1,  1, -1,  0,  1};

    for (int i = 0; i < 8; i++) {
        addMove(board, list, (Move){{r, c}, {r + dR[i], c + dC[i]}, EMPTY, MOVE_NORMAL});
    }
    
    // --- 2. Castling ---
    // ASSUMPTION: isKingInCheck() is from Person 2
    if (isKingInCheck(board, player)) {
        return; // Cannot castle out of check
    }

    // ASSUMPTION: isSquareAttacked() is available
    // ASSUMPTION: board->castling is managed by Person 2's makeMove()
    if (player == WHITE) {
        // White Kingside (wk)
        // Check rights, empty squares, and squares not attacked
        if (board->castling.wk &&
            board->squares[7][5].type == EMPTY &&
            board->squares[7][6].type == EMPTY &&
            !isSquareAttacked(board, 7, 5, opponent) && // King passes through f1
            !isSquareAttacked(board, 7, 6, opponent))   // King lands on g1
        {
            addMove(board, list, (Move){{7, 4}, {7, 6}, EMPTY, MOVE_CASTLE_KING});
        }
        // White Queenside (wq)
        if (board->castling.wq &&
            board->squares[7][1].type == EMPTY &&
            board->squares[7][2].type == EMPTY &&
            board->squares[7][3].type == EMPTY &&
            !isSquareAttacked(board, 7, 2, opponent) && // King lands on c1
            !isSquareAttacked(board, 7, 3, opponent))   // King passes through d1
        {
            addMove(board, list, (Move){{7, 4}, {7, 2}, EMPTY, MOVE_CASTLE_QUEEN});
        }
    } else { // BLACK
        // Black Kingside (bk)
        if (board->castling.bk &&
            board->squares[0][5].type == EMPTY &&
            board->squares[0][6].type == EMPTY &&
            !isSquareAttacked(board, 0, 5, opponent) && // King passes through f8
            !isSquareAttacked(board, 0, 6, opponent))   // King lands on g8
        {
            addMove(board, list, (Move){{0, 4}, {0, 6}, EMPTY, MOVE_CASTLE_KING});
        }
        // Black Queenside (bq)
        if (board->castling.bq &&
            board->squares[0][1].type == EMPTY &&
            board->squares[0][2].type == EMPTY &&
            board->squares[0][3].type == EMPTY &&
            !isSquareAttacked(board, 0, 2, opponent) && // King lands on c8
            !isSquareAttacked(board, 0, 3, opponent))   // King passes through d8
        {
            addMove(board, list, (Move){{0, 4}, {0, 2}, EMPTY, MOVE_CASTLE_QUEEN});
        }
    }
}

static void generateSlidingMoves(BoardState *board, MoveList *list, int r, int c) {
    Piece p = board->squares[r][c];

    // Direction vectors: 4 diagonal, 4 straight
    int dR[] = {-1, -1,  1,  1, -1,  1,  0,  0};
    int dC[] = {-1,  1, -1,  1,  0,  0, -1,  1};

    // Set loop bounds based on piece type
    int startDir = (p.type == BISHOP) ? 0 : (p.type == ROOK) ? 4 : 0;
    int endDir   = (p.type == BISHOP) ? 4 : (p.type == ROOK) ? 8 : 8; // Queen uses all 8

    for (int i = startDir; i < endDir; i++) {
        for (int k = 1; k < 8; k++) { // Iterate distance k
            int newR = r + dR[i] * k;
            int newC = c + dC[i] * k;

            if (newR < 0 || newR >= 8 || newC < 0 || newC >= 8) break; // Off board
            
            Piece target = board->squares[newR][newC];
            
            if (target.type == EMPTY) {
                // Empty square, add move and continue
                addMove(board, list, (Move){{r, c}, {newR, newC}, EMPTY, MOVE_NORMAL});
            } else if (target.color != board->currentPlayer) {
                // Opponent piece, add capture and stop in this direction
                addMove(board, list, (Move){{r, c}, {newR, newC}, EMPTY, MOVE_NORMAL});
                break; 
            } else {
                // Own piece, stop in this direction
                break; 
            }
        }
    }
}


/**
 * @brief Checks if a square (r, c) is attacked by any piece of attackerColor.
 *
 * NOTE: This is a *simplified* implementation for demonstration.
 * It's less efficient than a dedicated attack map.
 * Person 2 should ideally provide a fully vetted version in game.c.
 */
static bool isSquareAttacked(BoardState *board, int r, int c, PieceColor attackerColor) {
    // Check for attacks from sliding pieces (Queen, Rook, Bishop)
    int dR[] = {-1, -1,  1,  1, -1,  1,  0,  0};
    int dC[] = {-1,  1, -1,  1,  0,  0, -1,  1};
    for (int i = 0; i < 8; i++) {
        for (int k = 1; k < 8; k++) {
            int nR = r + dR[i] * k;
            int nC = c + dC[i] * k;
            if (nR < 0 || nR >= 8 || nC < 0 || nC >= 8) break;
            Piece p = board->squares[nR][nC];
            if (p.type != EMPTY) {
                if (p.color == attackerColor) {
                    if (p.type == QUEEN) return true;
                    if (i < 4 && p.type == BISHOP) return true; // Diagonal
                    if (i >= 4 && p.type == ROOK) return true; // Straight
                }
                break; // Blocked by another piece
            }
        }
    }

    // Check for attacks from knights
    int knR[] = {-2, -2, -1, -1,  1,  1,  2,  2};
    int knC[] = {-1,  1, -2,  2, -2,  2, -1,  1};
    for (int i = 0; i < 8; i++) {
        int nR = r + knR[i];
        int nC = c + knC[i];
        if (nR >= 0 && nR < 8 && nC >= 0 && nC < 8) {
            Piece p = board->squares[nR][nC];
            if (p.color == attackerColor && p.type == KNIGHT) return true;
        }
    }

    // Check for attacks from pawns
    int pawnDir = (attackerColor == WHITE) ? 1 : -1; // Attacker is white, they attack "down" (row++)
    if (r + pawnDir >= 0 && r + pawnDir < 8) {
        if (c - 1 >= 0) {
            Piece p = board->squares[r + pawnDir][c - 1];
            if (p.color == attackerColor && p.type == PAWN) return true;
        }
        if (c + 1 < 8) {
            Piece p = board->squares[r + pawnDir][c + 1];
            if (p.color == attackerColor && p.type == PAWN) return true;
        }
    }

    // Check for attacks from king
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            if (i == 0 && j == 0) continue;
            int nR = r + i;
            int nC = c + j;
            if (nR >= 0 && nR < 8 && nC >= 0 && nC < 8) {
                Piece p = board->squares[nR][nC];
                if (p.color == attackerColor && p.type == KING) return true;
            }
        }
    }
    
    return false;
}