/**
 * @file book.h
 * @brief A small, built-in opening book: a curated set of well-known
 * opening lines the engine can play instantly from, instead of spending
 * search time re-deriving well-established theory move by move.
 *
 * See docs/OPENING_BOOK.md for how the book is represented, matched
 * against the current position, and its known limitations.
 */

#ifndef BOOK_H
#define BOOK_H

#include "structs.h"
#include <stdbool.h>

/**
 * @brief Looks up the current position in the built-in opening book.
 *
 * @param board The current position. Only its piece placement, side to
 * move, and fullmove number are read.
 * @param out On success, filled with the book's suggested move (always a
 * plain MOVE_NORMAL move with no promotion - see docs/OPENING_BOOK.md for
 * why the book never needs castling/en passant/promotion flags).
 * @return true if a book entry matched (and the opening book is enabled
 * via setUseOpeningBook()), false otherwise - the caller should fall back
 * to its normal search.
 */
bool findBookMove(const BoardState *board, Move *out);

/**
 * @brief Enables or disables the opening book. Enabled by default.
 * @param enabled Whether findBookMove() should ever return a match.
 */
void setUseOpeningBook(bool enabled);

/** @return Whether the opening book is currently enabled. */
bool getUseOpeningBook(void);

#endif // BOOK_H
