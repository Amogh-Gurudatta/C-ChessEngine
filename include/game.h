/**
 * @file game.h
 * @brief Move application and attack/check detection - the rules engine
 * that everything else (move generation, search, notation) builds on.
 *
 * See docs/BOARD_AND_RULES.md for the full write-up, including the
 * make/undo history-stack design and its one sharp edge (it isn't reset by
 * loading a new position - see the note on undoMove() below).
 */

#ifndef GAME_H
#define GAME_H

#include "structs.h"
#include <stdbool.h>

/**
 * @brief Applies a move to the board in place: moves the piece, handles
 * captures/en passant/castling/promotion, updates castling rights, the
 * en passant target, the halfmove/fullmove counters, and flips
 * currentPlayer. Pushes enough state onto an internal history stack for a
 * matching undoMove() to reverse it exactly.
 *
 * @param board The position to mutate. Must not itself be in check-detection
 * use at the same time (this function is not reentrant with respect to a
 * single board's history stack - see undoMove()'s note).
 * @param move The move to apply. Its `flag` must already be resolved (e.g.
 * via notation.c's resolveMove()) - this function trusts it and does not
 * re-derive whether a move is really castling/en passant/promotion.
 */
void makeMove(BoardState *board, Move move);

/**
 * @brief Reverses the most recently applied makeMove() call, restoring the
 * board to exactly what it was before (including castling rights, en
 * passant target, and both move counters).
 *
 * @note The `move` parameter is accepted for symmetry with makeMove() and
 * to make call sites self-documenting, but the actual undo data comes from
 * an internal (file-static) history stack in game.c, not from this
 * parameter - always call undoMove() with the same move you just passed to
 * makeMove(), in matching pairs, and never call it after loading a
 * different position (e.g. via notation.c's fenToBoard()), since that
 * stack is not reset by loading a new position and would then unwind
 * moves from an unrelated game. This is why main.c's "undo" command
 * restores by re-loading a saved FEN snapshot instead of calling this
 * function directly - see docs/BOARD_AND_RULES.md.
 *
 * @param board The position to restore.
 * @param move The move being undone (see the note above on why its
 * contents don't actually drive the undo).
 */
void undoMove(BoardState *board, Move move);

/**
 * @brief Whether kingColor's king is currently under attack.
 *
 * @param board The position to check.
 * @param kingColor Which side's king to check.
 * @return true if that king is in check.
 */
bool isKingInCheck(BoardState *board, PieceColor kingColor);

/**
 * @brief Whether any attackerColor piece currently attacks square (r, c),
 * regardless of what (if anything) occupies that square. The core
 * primitive both isKingInCheck() and castling's "can't castle through
 * check" rule are built on.
 *
 * @param board The position to check.
 * @param r Row of the square to test (see structs.h's Position for the
 * row/col convention).
 * @param c Column of the square to test.
 * @param attackerColor The side whose attacks to check for.
 * @return true if the square is attacked by a piece of that color.
 */
bool isSquareAttacked(BoardState *board, int r, int c, PieceColor attackerColor);

#endif // GAME_H
