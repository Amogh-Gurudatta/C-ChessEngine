/**
 * @file timecontrol.h
 * @brief A Fischer chess clock (time + increment per side), shared between
 * local play (main.c's --clock) and Lichess play (lichess.c mirrors the
 * real clock the server reports instead of tracking its own).
 *
 * See docs/ONLINE_PLAY.md for how this integrates with the AI's time
 * management in ai.h.
 */

#ifndef TIMECONTROL_H
#define TIMECONTROL_H

#include "structs.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief A Fischer clock: each side has its own remaining time, and gains
 * a fixed increment after completing a move.
 */
typedef struct
{
    double whiteSeconds;
    double blackSeconds;
    double incrementSeconds;
} ChessClock;

/**
 * @brief Sets up a clock with the same starting time for both sides.
 *
 * @param clock The clock to initialize.
 * @param startSeconds Starting time for each side, in seconds.
 * @param incrementSeconds Seconds added to a side's clock after each move
 * it completes (0 for no increment).
 */
void clockInit(ChessClock *clock, double startSeconds, double incrementSeconds);

/**
 * @brief Returns a pointer to the given side's remaining time within the
 * clock, so callers can read or adjust it directly (e.g. lichess.c
 * overwrites it to mirror the server's authoritative clock instead of a
 * locally-tracked one).
 *
 * @param clock The clock to look inside.
 * @param side Which side's remaining-time field to return.
 * @return A pointer to clock->whiteSeconds or clock->blackSeconds.
 */
double *clockTimeFor(ChessClock *clock, PieceColor side);

/**
 * @brief Subtracts elapsedSeconds from side's remaining time, then - only
 * if that didn't deplete it - adds the increment. This ordering matters:
 * a side that has already run out of time doesn't get the increment back
 * (see main.c's use of this for the "flag-fall" rule: a move that arrives
 * after time has already run out doesn't count).
 *
 * @param clock The clock to update.
 * @param side Which side's time to consume.
 * @param elapsedSeconds How much wall-clock time that side just used.
 * @return false if the side just ran out of time (flagged), true otherwise.
 */
bool clockConsume(ChessClock *clock, PieceColor side, double elapsedSeconds);

/**
 * @brief Whether side's remaining time is at or below zero.
 *
 * @param clock The clock to check.
 * @param side Which side to check.
 * @return true if that side has flagged (run out of time).
 */
bool clockHasFlagged(ChessClock *clock, PieceColor side);

/**
 * @brief Formats seconds as "m:ss" (or "h:mm:ss" past an hour) for display.
 *
 * @param seconds The duration to format. Negative values are clamped to 0
 * rather than producing garbage (e.g. for an already-flagged clock).
 * @param buf Destination buffer.
 * @param bufSize Size of buf; 16 bytes is always enough.
 */
void clockFormat(double seconds, char *buf, size_t bufSize);

#endif // TIMECONTROL_H
