/**
 * @file notation.h
 * @brief All move-text and position-text conversion: long algebraic ("e2e4"),
 * Standard Algebraic Notation ("Nf3"), real FEN, PGN export, and the
 * position-key used for threefold-repetition detection.
 *
 * See docs/NOTATION_AND_FORMATS.md for the grammar each format accepts and
 * how this differs from fileio.h's separate, older custom save format.
 */

#ifndef NOTATION_H
#define NOTATION_H

#include "structs.h"
#include <stdbool.h>
#include <stddef.h>

/* ---------------- Long algebraic ("e2e4", "a7a8q") ---------------- */

/**
 * @brief Parses "e2e4" / "a7a8q" into a raw Move. The result is "raw" in
 * that its `flag` is not yet resolved against legality - it doesn't know
 * whether e.g. "e1g1" means a normal king move or castling; pass it to
 * resolveMove() for that.
 *
 * @param s The string to parse. Must be exactly 4 or 5 characters (see
 * isLongAlgebraicFormat()).
 * @param out Filled with the parsed move. On failure, out->from.row and
 * out->from.col are set to -1 as an invalid-move sentinel.
 * @return true if the string had a plausible shape (see
 * isLongAlgebraicFormat()) and was parsed; this does NOT mean the move is
 * legal, only that it parsed.
 */
bool parseLongAlgebraic(const char *s, Move *out);

/**
 * @brief Matches a raw from/to (and optional promotion) against the
 * current player's legal moves, to recover the correct flag
 * (MOVE_NORMAL/MOVE_CASTLE_KING/MOVE_CASTLE_QUEEN/MOVE_EN_PASSANT/
 * MOVE_PROMOTION) and, for a promotion with no piece specified, defaults
 * to queen.
 *
 * @param board The current position (its legal moves are generated fresh
 * on each call via ai.h's generateAllLegalMoves()).
 * @param raw A move as produced by parseLongAlgebraic() (only from/to/
 * promotion are read).
 * @param out Filled with the resolved, fully-legal move on success.
 * @return true if raw's from/to (and promotion, if any) matched exactly
 * one legal move; false if it didn't match any (illegal move).
 */
bool resolveMove(BoardState *board, Move raw, Move *out);

/**
 * @brief Formats a Move as long algebraic notation, e.g. "e2e4" or "a7a8q".
 *
 * @param m The move to format.
 * @param buf Destination buffer.
 * @param bufSize Size of buf; 6 bytes is always enough.
 */
void moveToLongAlgebraic(Move m, char *buf, size_t bufSize);

/**
 * @brief Whether s has the shape of long algebraic notation (4-5
 * characters, `[a-h][1-8][a-h][1-8]` optionally followed by a promotion
 * letter) as opposed to SAN. Used by parseUserMove() to decide which
 * parser to try.
 *
 * @param s The string to check.
 * @return true if s looks like long algebraic notation.
 */
bool isLongAlgebraicFormat(const char *s);

/* ---------------- Standard Algebraic Notation (SAN) ---------------- */

/**
 * @brief Parses SAN ("e4", "Nf3", "O-O", "exd5", "e8=Q", "Qxe7+") against
 * the current legal moves for board->currentPlayer, resolving piece type,
 * disambiguation (file/rank/both, when more than one like piece could
 * reach the same square), captures, castling, and promotion.
 *
 * @param board The current position.
 * @param san The SAN string to parse. Trailing '+'/'#' are accepted and
 * ignored (not validated against whether the move is actually a check).
 * @param out Filled with the resolved move on success.
 * @return true if san matched exactly one legal move; false if it didn't
 * parse, or matched zero or more than one (ambiguous) legal moves.
 */
bool sanToMove(BoardState *board, const char *san, Move *out);

/**
 * @brief Formats a legal move as SAN, including disambiguation and a
 * trailing '+' or '#' when the move gives check or checkmate.
 *
 * @param board The position BEFORE the move (this function internally
 * calls makeMove()/undoMove() to detect check/checkmate, then restores
 * board exactly - see game.h's undoMove() for why that round-trip is safe
 * here but not across a position reload).
 * @param m The move to format (must be legal in board).
 * @param buf Destination buffer.
 * @param bufSize Size of buf; 12 bytes (SAN_MAX_LEN) is always enough.
 */
void moveToSan(BoardState *board, Move m, char *buf, size_t bufSize);

/**
 * @brief The dispatcher main.c and lichess.c use for all typed move input:
 * tries long algebraic first (isLongAlgebraicFormat() + parseLongAlgebraic()
 * + resolveMove()), and falls back to SAN (sanToMove()) otherwise.
 *
 * @param board The current position.
 * @param input The raw text the user typed.
 * @param out Filled with the resolved move on success.
 * @return true if input was a legal move in either notation.
 */
bool parseUserMove(BoardState *board, const char *input, Move *out);

/* ---------------- FEN ---------------- */

/** Big enough for any legal FEN string (piece placement, side to move,
 * castling rights, en passant square, and both move counters). */
#define FEN_MAX_LEN 96

/**
 * @brief Serializes board to a standard FEN string (all six fields -
 * unlike fileio.h's older, unrelated custom save format).
 *
 * @param board The position to serialize.
 * @param buf Destination buffer.
 * @param bufSize Size of buf; FEN_MAX_LEN is always enough.
 * @return true on success; false only if bufSize was too small.
 */
bool boardToFen(const BoardState *board, char *buf, size_t bufSize);

/**
 * @brief Parses a standard FEN string into board, overwriting it entirely.
 *
 * @param fen The FEN string to parse. Must have at least the first four
 * fields (piece placement, side to move, castling rights, en passant
 * square); the halfmove clock and fullmove number default to 0 and 1 if
 * omitted.
 * @param board Overwritten with the parsed position on success.
 * @return true on success, false if fen is malformed.
 */
bool fenToBoard(const char *fen, BoardState *board);

/**
 * @brief Builds a key identifying the position for threefold-repetition
 * purposes: piece placement, side to move, castling rights, and en passant
 * square - i.e. FEN without the halfmove/fullmove counters, since those
 * change every move and would otherwise defeat matching one occurrence of
 * a position against another (see docs/BOARD_AND_RULES.md's section on
 * draw detection).
 *
 * @param board The position to build a key for.
 * @param buf Destination buffer.
 * @param bufSize Size of buf; FEN_MAX_LEN is always enough.
 */
void boardToPositionKey(const BoardState *board, char *buf, size_t bufSize);

/* ---------------- PGN ---------------- */

/** Big enough for any SAN move string, including a disambiguated
 * promotion with check, e.g. "Qh4xe7=Q+". */
#define SAN_MAX_LEN 12

/**
 * @brief Writes an accumulated SAN move log out as a standard PGN file
 * with the seven-tag roster and numbered move text.
 *
 * @param filename Path to write to (overwritten if it already exists).
 * @param sanLog The moves played so far, in order, as produced by
 * moveToSan().
 * @param moveCount Number of entries in sanLog.
 * @param whiteName Name to record in the White tag.
 * @param blackName Name to record in the Black tag.
 * @param result The game's result tag: "1-0", "0-1", "1/2-1/2", or "*" if
 * still in progress.
 * @return true on success, false if the file couldn't be opened for writing.
 */
bool exportPgn(const char *filename, char sanLog[][SAN_MAX_LEN], int moveCount,
               const char *whiteName, const char *blackName, const char *result);

#endif // NOTATION_H
