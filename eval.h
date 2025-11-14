#ifndef EVAL_H
#define EVAL_H

#include "structs.h" // From Person 1

/**
 * @brief Evaluates the current board state and returns a static score.
 *
 * The score is calculated from White's perspective.
 * A positive score favors WHITE.
 * A negative score favors BLACK.
 * The score is based on material and piece-square tables.
 *
 * @param board The current board state to evaluate.
 * @return The static evaluation score (int).
 */
int evaluateBoard(BoardState *board);

#endif // EVAL_H