#ifndef FILEIO_H
#define FILEIO_H

#include "structs.h"
#include <stdbool.h>

// Load board from a text file (simple 8x8 layout)
bool loadBoardFromFile(const char *filename, BoardState *board);

// Save board to text file
bool saveBoardToFile(const char *filename, const BoardState *board);

// Convert a piece to character (uppercase = white, lowercase = black)
char pieceToChar(Piece p);

// Convert character from file to piece
Piece charToPiece(char c);

#endif
