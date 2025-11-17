#include "game.h"
#include <string.h>
#include <stdio.h>

/*
 * Implementation notes / invariants:
 * - This file keeps an internal history stack to allow undoMove() to restore
 *   previous board state. The history only stores what we need: captured piece,
 *   castling rights, enPassantTarget, halfmove clock and fullmove number and the move flag.
 *
 * - The layout of BoardState/squares/currentPlayer/castling/enPassantTarget/halfmoveClock
 *   must match the assumptions stated above.
 */

/* ---------- Internal helper types ---------- */

typedef struct {
    Move move;
    Piece captured;
    CastlingRights prevCastling;
    Position prevEnPassant;
    int prevHalfmoveClock;
    int prevFullmoveNumber;
    PieceColor prevPlayer;
} MoveRecord;

#define MAX_HISTORY 4096
static MoveRecord historyStack[MAX_HISTORY];
static int historyTop = 0;

/* ---------- Helper functions ---------- */

static void pushHistory(MoveRecord rec) {
    if (historyTop < MAX_HISTORY) {
        historyStack[historyTop++] = rec;
    } else {
        // overflow — in practice shouldn't happen; but drop oldest (not ideal)
        // For safety, cap
        historyTop = MAX_HISTORY - 1;
        historyStack[historyTop] = rec;
    }
}

static MoveRecord popHistory(void) {
    MoveRecord empty = {0};
    if (historyTop <= 0) return empty;
    return historyStack[--historyTop];
}

static bool onBoard(int r, int c) {
    return (r >= 0 && r < 8 && c >= 0 && c < 8);
}

/* Find king position for a color */
static Position findKing(BoardState *board, PieceColor color) {
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) {
        Piece p = board->squares[r][c];
        if (p.type == KING && p.color == color) return (Position){r, c};
    }
    return (Position){-1, -1};
}

/* Remove castling rights if rook or king moved/captured */
static void updateCastlingRights_afterMove(BoardState *board, Position from, Position to, Piece movedPiece) {
    // If king moved, remove both castling rights for that side
    if (movedPiece.type == KING) {
        if (movedPiece.color == WHITE) { board->castling.wk = 0; board->castling.wq = 0; }
        else { board->castling.bk = 0; board->castling.bq = 0; }
    }

    // If rook moved from its original square, remove appropriate right
    if (movedPiece.type == ROOK) {
        if (movedPiece.color == WHITE) {
            if (from.row == 7 && from.col == 0) board->castling.wq = 0;
            if (from.row == 7 && from.col == 7) board->castling.wk = 0;
        } else {
            if (from.row == 0 && from.col == 0) board->castling.bq = 0;
            if (from.row == 0 && from.col == 7) board->castling.bk = 0;
        }
    }

    // If a rook is captured on its original square, remove corresponding right
    Piece captured = board->squares[to.row][to.col];
    (void) captured; // captured used in calling code if needed
}

/* Helper to clear enPassantTarget */
static void clearEnPassant(BoardState *board) {
    board->enPassantTarget.row = -1;
    board->enPassantTarget.col = -1;
}

/* ---------- Public API implementations ---------- */

void makeMove(BoardState *board, Move move) {
    // Save previous state into record
    MoveRecord rec;
    rec.move = move;
    rec.prevCastling = board->castling;
    rec.prevEnPassant = board->enPassantTarget;
    rec.prevHalfmoveClock = board->halfmoveClock;
    rec.prevFullmoveNumber = board->fullmoveNumber;
    rec.prevPlayer = board->currentPlayer;

    Position from = move.from;
    Position to   = move.to;
    Piece moving = board->squares[from.row][from.col];
    rec.captured = board->squares[to.row][to.col]; // may be EMPTY

    // Default: increment halfmove clock when no capture and no pawn move
    bool resetHalfmove = false;

    // 1) Handle special move types
    if (move.flag == MOVE_CASTLE_KING || move.flag == MOVE_CASTLE_QUEEN) {
        // move king
        board->squares[to.row][to.col] = moving;
        board->squares[from.row][from.col] = (Piece){EMPTY, NO_COLOR};

        // Move the rook accordingly
        if (moving.color == WHITE) {
            if (move.flag == MOVE_CASTLE_KING) {
                // White king e1->g1, rook h1->f1
                board->squares[7][5] = board->squares[7][7];
                board->squares[7][7] = (Piece){EMPTY, NO_COLOR};
            } else {
                // queenside: e1->c1, rook a1->d1
                board->squares[7][3] = board->squares[7][0];
                board->squares[7][0] = (Piece){EMPTY, NO_COLOR};
            }
            board->castling.wk = board->castling.wq = 0;
        } else {
            if (move.flag == MOVE_CASTLE_KING) {
                // Black e8->g8, rook h8->f8
                board->squares[0][5] = board->squares[0][7];
                board->squares[0][7] = (Piece){EMPTY, NO_COLOR};
            } else {
                // Black queenside
                board->squares[0][3] = board->squares[0][0];
                board->squares[0][0] = (Piece){EMPTY, NO_COLOR};
            }
            board->castling.bk = board->castling.bq = 0;
        }

        // After castling, reset en passant
        clearEnPassant(board);
        resetHalfmove = true; // king move -> reset halfmove clock
    }
    else if (move.flag == MOVE_EN_PASSANT) {
        // Normal move of pawn to capture en-passant target square
        board->squares[to.row][to.col] = moving;
        board->squares[from.row][from.col] = (Piece){EMPTY, NO_COLOR};

        // Remove the captured pawn which is behind the to-square
        int capRow = rec.prevPlayer == WHITE ? to.row + 1 : to.row - 1;
        if (onBoard(capRow, to.col)) {
            rec.captured = board->squares[capRow][to.col];
            board->squares[capRow][to.col] = (Piece){EMPTY, NO_COLOR};
        }
        clearEnPassant(board);
        resetHalfmove = true; // pawn capture resets halfmove clock
    }
    else {
        // Normal move or promotion or normal capture
        // If move is a promotion, place promoted piece
        if (move.flag == MOVE_PROMOTION && (moving.type == PAWN)) {
            Piece promoted = { move.promotion, moving.color };
            board->squares[to.row][to.col] = promoted;
            board->squares[from.row][from.col] = (Piece){EMPTY, NO_COLOR};
            // Promotion resets halfmove clock
            resetHalfmove = true;
        } else {
            // Normal move
            // Copy moving piece to destination (overwriting captured)
            board->squares[to.row][to.col] = moving;
            board->squares[from.row][from.col] = (Piece){EMPTY, NO_COLOR};

            // If capture occurred set resetHalfmove
            if (rec.captured.type != EMPTY) resetHalfmove = true;
        }

        // If pawn moved two squares, set enPassant target (square behind pawn)
        if (moving.type == PAWN && abs(to.row - from.row) == 2) {
            // enPassantTarget is the square behind the pawn (the square it jumped over)
            int epRow = (from.row + to.row) / 2;
            board->enPassantTarget.row = epRow;
            board->enPassantTarget.col = from.col;
            // Pawn move -> reset halfmove clock
            resetHalfmove = true;
        } else {
            // Not a double pawn move -> clear enPassant
            clearEnPassant(board);
        }
    }

    // 2) Update castling rights when rooks/king move or when rook is captured on original squares
    // Removing rights if a rook is captured on starting square:
    // Check capture of white rook at a1/h1 or black rook at a8/h8
    if (rec.captured.type == ROOK) {
        if (rec.captured.color == WHITE) {
            if (to.row == 7 && to.col == 0) board->castling.wq = 0;
            if (to.row == 7 && to.col == 7) board->castling.wk = 0;
        } else {
            if (to.row == 0 && to.col == 0) board->castling.bq = 0;
            if (to.row == 0 && to.col == 7) board->castling.bk = 0;
        }
    }

    // If a rook moved, update earlier (we already did in castling branch) — still check:
    if (moving.type == ROOK) {
        if (moving.color == WHITE) {
            if (from.row == 7 && from.col == 0) board->castling.wq = 0;
            if (from.row == 7 && from.col == 7) board->castling.wk = 0;
        } else {
            if (from.row == 0 && from.col == 0) board->castling.bq = 0;
            if (from.row == 0 && from.col == 7) board->castling.bk = 0;
        }
    }

    if (moving.type == KING) {
        if (moving.color == WHITE) { board->castling.wk = 0; board->castling.wq = 0; }
        else { board->castling.bk = 0; board->castling.bq = 0; }
    }

    // 3) Update halfmove clock and fullmove number
    if (resetHalfmove) board->halfmoveClock = 0;
    else board->halfmoveClock++;

    if (board->currentPlayer == BLACK) {
        board->fullmoveNumber++;
    }

    // 4) Switch player
    board->currentPlayer = (board->currentPlayer == WHITE) ? BLACK : WHITE;

    // 5) Push history record
    pushHistory(rec);
}

void undoMove(BoardState *board, Move move) {
    // Pop history
    if (historyTop <= 0) {
        // Nothing to undo
        return;
    }
    MoveRecord rec = popHistory();

    Position from = rec.move.from;
    Position to   = rec.move.to;
    Piece movedPiece; // we need to find what piece is currently on 'to' (or special handling)

    // Switch player back first (since makeMove switched it)
    board->currentPlayer = rec.prevPlayer;

    // Restore fullmove and halfmove
    board->halfmoveClock = rec.prevHalfmoveClock;
    board->fullmoveNumber = rec.prevFullmoveNumber;
    board->castling = rec.prevCastling;
    board->enPassantTarget = rec.prevEnPassant;

    // Handle special move undo
    if (rec.move.flag == MOVE_CASTLE_KING || rec.move.flag == MOVE_CASTLE_QUEEN) {
        // King should be at 'to' and rook at f1/d1 or f8/d8; restore original positions
        movedPiece = board->squares[to.row][to.col];
        // Move king back
        board->squares[from.row][from.col] = movedPiece;
        board->squares[to.row][to.col] = (Piece){EMPTY, NO_COLOR};

        // Restore rook
        if (rec.prevPlayer == WHITE) {
            if (rec.move.flag == MOVE_CASTLE_KING) {
                // rook f1 -> h1
                board->squares[7][7] = board->squares[7][5];
                board->squares[7][5] = (Piece){EMPTY, NO_COLOR};
            } else {
                // rook d1 -> a1
                board->squares[7][0] = board->squares[7][3];
                board->squares[7][3] = (Piece){EMPTY, NO_COLOR};
            }
        } else {
            if (rec.move.flag == MOVE_CASTLE_KING) {
                board->squares[0][7] = board->squares[0][5];
                board->squares[0][5] = (Piece){EMPTY, NO_COLOR};
            } else {
                board->squares[0][0] = board->squares[0][3];
                board->squares[0][3] = (Piece){EMPTY, NO_COLOR};
            }
        }
    }
    else if (rec.move.flag == MOVE_EN_PASSANT) {
        // The pawn moved to 'to' and captured pawn is behind it (in rec.captured)
        movedPiece = board->squares[to.row][to.col];
        // Move pawn back
        board->squares[from.row][from.col] = movedPiece;
        board->squares[to.row][to.col] = (Piece){EMPTY, NO_COLOR};
        // Restore captured pawn
        int capRow = (rec.prevPlayer == WHITE) ? to.row + 1 : to.row - 1;
        if (onBoard(capRow, to.col)) {
            board->squares[capRow][to.col] = rec.captured;
        }
    }
    else {
        // Normal move or promotion
        Piece destPiece = board->squares[to.row][to.col];
        // If promotion happened, current dest holds promoted piece; put pawn back
        if (rec.move.flag == MOVE_PROMOTION) {
            // Restore pawn
            Piece pawn = {PAWN, rec.prevPlayer};
            board->squares[from.row][from.col] = pawn;
            // Restore captured piece (if any) on destination
            board->squares[to.row][to.col] = rec.captured;
        } else {
            // Normal move: move piece back and restore captured
            board->squares[from.row][from.col] = board->squares[to.row][to.col];
            board->squares[to.row][to.col] = rec.captured;
        }
    }
}

bool isSquareAttacked(BoardState *board, int r, int c, PieceColor attackerColor) {
    // Sliding pieces (rook, bishop, queen)
    int dR[] = {-1, -1,  1,  1, -1,  1,  0,  0};
    int dC[] = {-1,  1, -1,  1,  0,  0, -1,  1};
    for (int i = 0; i < 8; i++) {
        for (int k = 1; k < 8; k++) {
            int nR = r + dR[i] * k;
            int nC = c + dC[i] * k;
            if (!onBoard(nR, nC)) break;
            Piece p = board->squares[nR][nC];
            if (p.type != EMPTY) {
                if (p.color == attackerColor) {
                    if (p.type == QUEEN) return true;
                    if (i < 4 && p.type == BISHOP) return true; // diagonal directions 0..3
                    if (i >= 4 && p.type == ROOK) return true;  // straight directions 4..7
                }
                break; // blocked
            }
        }
    }

    // Knights
    int knR[] = {-2, -2, -1, -1,  1,  1,  2,  2};
    int knC[] = {-1,  1, -2,  2, -2,  2, -1,  1};
    for (int i = 0; i < 8; i++) {
        int nR = r + knR[i], nC = c + knC[i];
        if (!onBoard(nR, nC)) continue;
        Piece p = board->squares[nR][nC];
        if (p.color == attackerColor && p.type == KNIGHT) return true;
    }

    // Pawns (attack squares differ by color)
    int pawnDir = (attackerColor == WHITE) ? -1 : 1; // attacker white pawns move "up" (row--)
    // Note: ai.c earlier had a different pawnDir convention; ensure consistency:
    // Here: if attacker is WHITE, they attack from r-1 (towards smaller row)
    int pr = r + pawnDir;
    if (pr >= 0 && pr < 8) {
        if (c - 1 >= 0) {
            Piece p = board->squares[pr][c - 1];
            if (p.color == attackerColor && p.type == PAWN) return true;
        }
        if (c + 1 < 8) {
            Piece p = board->squares[pr][c + 1];
            if (p.color == attackerColor && p.type == PAWN) return true;
        }
    }

    // King (one-square adjacency)
    for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++) {
        if (i == 0 && j == 0) continue;
        int nR = r + i, nC = c + j;
        if (!onBoard(nR, nC)) continue;
        Piece p = board->squares[nR][nC];
        if (p.color == attackerColor && p.type == KING) return true;
    }

    return false;
}

bool isKingInCheck(BoardState *board, PieceColor kingColor) {
    Position kp = findKing(board, kingColor);
    if (kp.row == -1) {
        // No king found — treat as not in check
        return false;
    }
    PieceColor attacker = (kingColor == WHITE) ? BLACK : WHITE;
    return isSquareAttacked(board, kp.row, kp.col, attacker);
}
