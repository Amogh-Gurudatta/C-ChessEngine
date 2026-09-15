#ifndef NOTATION_H
#define NOTATION_H

#include "structs.h"
#include <stdbool.h>
#include <stddef.h>

/* ---------------- Long algebraic ("e2e4", "a7a8q") ---------------- */

/* Parses "e2e4" / "a7a8q" into a raw Move (flag/promotion not yet resolved
 * against legality). On failure sets out->from.row = -1. */
bool parseLongAlgebraic(const char *s, Move *out);

/* Matches a raw from/to (and optional promotion) against the current
 * player's legal moves to recover the correct flag (castle/en-passant/
 * promotion) and exact promotion piece. */
bool resolveMove(BoardState *board, Move raw, Move *out);

/* Formats a Move as long algebraic notation, e.g. "e2e4" or "a7a8q". */
void moveToLongAlgebraic(Move m, char *buf, size_t bufSize);

/* True if s looks like long-algebraic notation (as opposed to SAN). */
bool isLongAlgebraicFormat(const char *s);

/* ---------------- Standard Algebraic Notation (SAN) ---------------- */

/* Parses SAN ("e4", "Nf3", "O-O", "exd5", "e8=Q", "Qxe7+") against the
 * current legal moves for board->currentPlayer. */
bool sanToMove(BoardState *board, const char *san, Move *out);

/* Formats a legal move as SAN. board must be in the PRE-move state. */
void moveToSan(BoardState *board, Move m, char *buf, size_t bufSize);

/* Dispatcher used by the game loop: tries long algebraic first (to keep
 * existing behavior identical), then falls back to SAN. */
bool parseUserMove(BoardState *board, const char *input, Move *out);

/* ---------------- FEN ---------------- */

#define FEN_MAX_LEN 96

/* Serializes board to a standard FEN string. */
bool boardToFen(const BoardState *board, char *buf, size_t bufSize);

/* Parses a standard FEN string into board. */
bool fenToBoard(const char *fen, BoardState *board);

/* Builds a key identifying the position for threefold-repetition purposes:
 * piece placement, side to move, castling rights, and en-passant square -
 * i.e. FEN without the halfmove/fullmove counters, since those don't
 * affect whether two positions count as "the same" for repetition. */
void boardToPositionKey(const BoardState *board, char *buf, size_t bufSize);

/* ---------------- PGN ---------------- */

#define SAN_MAX_LEN 12

/* Writes an accumulated SAN move log out as a PGN file. */
bool exportPgn(const char *filename, char sanLog[][SAN_MAX_LEN], int moveCount,
               const char *whiteName, const char *blackName, const char *result);

#endif // NOTATION_H
