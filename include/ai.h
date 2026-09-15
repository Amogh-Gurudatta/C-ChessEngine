/**
 * @file ai.h
 * @brief Move generation and the search that picks the engine's move:
 * iterative-deepening NegaMax with alpha-beta pruning, quiescence search,
 * and MVV-LVA move ordering, plus adjustable depth/time controls.
 *
 * See docs/SEARCH_AND_EVAL.md for how the search and time management work,
 * and docs/BOARD_AND_RULES.md for the make/undo rules this builds on.
 */

#ifndef AI_H
#define AI_H

#include "structs.h"
#include <stdbool.h>

/**
 * @brief Finds the best move for the current player using iterative-
 * deepening NegaMax (see docs/SEARCH_AND_EVAL.md): it searches depth 1,
 * then 2, then 3, and so on up to getSearchDepth(), keeping the move from
 * the last depth that finished completely before the current time cap
 * (setSearchTimeLimit()) expired.
 *
 * @param board The current state of the game board.
 * @return The best Move found by the AI. If board->squares has no legal
 * moves for the side to move, the returned Move's `from.row` is -1 - the
 * caller is expected to have already checked for checkmate/stalemate via
 * generateAllLegalMoves() before calling this.
 */
Move findBestMove(BoardState *board);

/**
 * @brief Generates every fully legal move for board->currentPlayer:
 * pseudo-legal moves are generated per-piece, then each is tried with
 * makeMove()/undoMove() and discarded if it would leave that side's own
 * king in check.
 *
 * @param board The position to generate moves for.
 * @return The list of legal moves. An empty list means the side to move is
 * either checkmated (if isKingInCheck() is also true) or stalemated.
 */
MoveList generateAllLegalMoves(BoardState *board);

/**
 * @brief Whether neither side has enough material left to possibly deliver
 * checkmate (bare kings, or a lone minor piece). Used both as a search-time
 * heuristic (negamax returns a draw score immediately) and, exposed here,
 * as a real draw condition the game loop can declare.
 *
 * @note This is a simplified check: it only recognizes the "at most one
 * minor piece total" cases, so positions like two same-colored bishops
 * (also drawn under FIDE rules) are reported as sufficient material.
 *
 * @param board The position to check.
 * @return true if the position is dead-drawn on material alone.
 */
bool isInsufficientMaterial(BoardState *board);

/**
 * @brief Sets how many half-move plies findBestMove() searches to (its
 * "quality ceiling" - a real clock, via findBestMoveTimed(), decides how
 * much of this ceiling actually gets used on any given move).
 *
 * @param depth Higher is stronger but slower. Values below 1 are ignored,
 * leaving the previous depth in effect.
 */
void setSearchDepth(int depth);

/** @return The current search depth (see setSearchDepth()). */
int getSearchDepth(void);

/**
 * @brief Sets a hard wall-clock cap on findBestMove(): it iteratively
 * deepens (depth 1, 2, 3, ...) up to getSearchDepth(), and returns the best
 * move from the last depth that finished before this many seconds elapsed.
 *
 * @param seconds The cap, in seconds. A value of 0 or less disables it
 * (search purely by depth, with no time limit).
 */
void setSearchTimeLimit(double seconds);

/** @return The current time cap in seconds (see setSearchTimeLimit()). */
double getSearchTimeLimit(void);

/**
 * @brief Decides how many seconds to spend searching one move, given how
 * much time is left on the clock, the increment, and the position's game
 * phase (eval.h's getGamePhase()). See docs/SEARCH_AND_EVAL.md for the
 * exact heuristic (more time in a complex middlegame, less in a bare
 * endgame, a hard "panic mode" cutoff when critically low).
 *
 * @param board The current position (only its game phase is read).
 * @param remainingSeconds Time left on this side's clock.
 * @param incrementSeconds Fischer increment gained after each move (0 if none).
 * @return Seconds to allow findBestMove() to spend on this move.
 */
double computeMoveTimeBudget(BoardState *board, double remainingSeconds, double incrementSeconds);

/**
 * @brief Like findBestMove(), but manages its own per-move time budget
 * from the actual clock (via computeMoveTimeBudget()) instead of using
 * whatever setSearchTimeLimit() was last set to, and restores the previous
 * time limit afterward. Use this instead of findBestMove() whenever the
 * game is played under a real clock (main.c's --clock, or lichess.c's
 * --bot mode using the real wtime/btime Lichess reports).
 *
 * @param board The current state of the game board.
 * @param remainingSeconds Time left on the side-to-move's clock.
 * @param incrementSeconds Fischer increment gained after each move (0 if none).
 * @return The best move found within the computed budget.
 */
Move findBestMoveTimed(BoardState *board, double remainingSeconds, double incrementSeconds);

#endif // AI_H
