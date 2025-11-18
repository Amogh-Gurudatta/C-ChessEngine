#ifndef AI_H
#define AI_H

#include "structs.h" // From Person 1

/**
 * @brief Finds the best move for the current player using the Minimax algorithm.
 *
 * This is the main entry point for the AI. It will search to a
 * predefined depth and return the optimal move it finds.
 *
 * @param board The current state of the game board.
 * @return The best Move found by the AI.
 */
Move findBestMove(BoardState *board);

/* Expose helper to allow callers to obtain the list of legal moves */
MoveList generateAllLegalMoves(BoardState *board);

#endif // AI_H