#ifndef GAME_H
#define GAME_H

#include "structs.h"
#include <stdbool.h>

/* Public functions used by other modules (ai.c, eval.c, etc.) */
void makeMove(BoardState *board, Move move);
void undoMove(BoardState *board, Move move);
bool isKingInCheck(BoardState *board, PieceColor kingColor);
bool isSquareAttacked(BoardState *board, int r, int c, PieceColor attackerColor);

#endif // GAME_H
