/**
 * @file fileio.h
 * @brief Save/load persistence via a simple custom text format
 * (`board.txt`), plus the piece<->character conversion notation.c and
 * other modules reuse for FEN's piece-placement field.
 *
 * @note This is *not* FEN, despite looking similar - see
 * docs/NOTATION_AND_FORMATS.md for exactly how the two differ and why
 * notation.c's boardToFen()/fenToBoard() are separate from this file.
 */

#ifndef FILEIO_H
#define FILEIO_H

#include "structs.h"
#include <stdbool.h>

/**
 * @brief Loads a board previously written by saveBoardToFile().
 *
 * @param filename Path to read from.
 * @param board Overwritten with the loaded position on success; left
 * untouched (and unspecified) on failure.
 * @return true on success, false if the file doesn't exist or is malformed.
 */
bool loadBoardFromFile(const char *filename, BoardState *board);

/**
 * @brief Writes a board to a simple custom text format: 8 rows of
 * pieceToChar() characters, then side-to-move, castling rights,
 * en-passant square, halfmove clock, and fullmove number - one per line.
 *
 * @param filename Path to write to (overwritten if it already exists).
 * @param board The position to save.
 * @return true on success, false if the file couldn't be opened for writing.
 */
bool saveBoardToFile(const char *filename, const BoardState *board);

/**
 * @brief Converts a piece to its FEN/board.txt character: uppercase for
 * White, lowercase for Black (e.g. 'N' for a white knight, 'n' for black),
 * or '.' for an empty square.
 *
 * @param p The piece to convert.
 * @return The corresponding character.
 */
char pieceToChar(Piece p);

/**
 * @brief The inverse of pieceToChar(): parses a single FEN/board.txt piece
 * character back into a Piece.
 *
 * @param c The character to parse. '.' or ' ' both parse as an empty square.
 * @return The corresponding Piece, or {EMPTY, NO_COLOR} for any character
 * that isn't a recognized piece letter.
 */
Piece charToPiece(char c);

#endif
