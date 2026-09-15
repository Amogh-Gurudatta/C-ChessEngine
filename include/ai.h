#ifndef AI_H
#define AI_H

#include "structs.h"
#include <stdbool.h>

/**
 * @brief Finds the best move for the current player using the Negamax algorithm.
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

/* Expose the draw-by-insufficient-material check so the game loop can
 * declare it as a real draw, not just use it as a search heuristic. */
bool isInsufficientMaterial(BoardState *board);

/* Adjustable AI difficulty: the number of half-move plies findBestMove
 * searches. Higher is stronger but slower. Values below 1 are ignored. */
void setSearchDepth(int depth);
int getSearchDepth(void);

/* Time cap for findBestMove, in seconds: it iteratively deepens (depth 1, 2,
 * 3, ...) up to the configured search depth, and returns the best move from
 * the last depth that finished before this many seconds elapsed. A value of
 * 0 or less disables the cap (search purely by depth). */
void setSearchTimeLimit(double seconds);
double getSearchTimeLimit(void);

/* Clock-aware move selection: computes how many seconds to spend on this
 * move from the actual remaining time, increment, and game phase (see
 * ai.c for the reasoning), then searches within that budget. Use this
 * instead of findBestMove() when the game is played under a real clock. */
double computeMoveTimeBudget(BoardState *board, double remainingSeconds, double incrementSeconds);
Move findBestMoveTimed(BoardState *board, double remainingSeconds, double incrementSeconds);

#endif // AI_H