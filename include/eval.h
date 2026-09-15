/**
 * @file eval.h
 * @brief Static position evaluation: tapered material + piece-square-table
 * scoring, blended between middlegame and endgame weights by game phase.
 *
 * See docs/SEARCH_AND_EVAL.md for how this feeds into the search in ai.c.
 */

#ifndef EVAL_H
#define EVAL_H

#include "structs.h"

/**
 * @brief Evaluates the current board state and returns a static score.
 *
 * The score is calculated from White's perspective.
 * A positive score favors WHITE.
 * A negative score favors BLACK.
 * The score is based on material, piece-square tables, and mobility,
 * tapered between middlegame and endgame weights by getGamePhase().
 *
 * @param board The current board state to evaluate.
 * @return The static evaluation score (int), in roughly centipawn units.
 */
int evaluateBoard(BoardState *board);

/**
 * @brief How far the game has progressed materially, from 24 (full opening
 * material) down to 0 (bare kings). Used both to taper evaluateBoard's
 * middlegame/endgame blend and, externally, as a game-phase signal for the
 * AI's time management (ai.h's findBestMoveTimed/computeMoveTimeBudget).
 *
 * @param board The position to measure.
 * @return The phase value, in [0, 24]. Knights/bishops count 1 each,
 * rooks 2, queens 4 (pawns and kings don't affect the phase).
 */
int getGamePhase(BoardState *board);

#endif // EVAL_H