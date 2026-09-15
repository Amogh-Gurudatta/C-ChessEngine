#ifndef TIMECONTROL_H
#define TIMECONTROL_H

#include "structs.h"
#include <stdbool.h>
#include <stddef.h>

/* A Fischer clock: each side has its own remaining time, and gains a fixed
 * increment after completing a move. Shared between main.c (local play)
 * and lichess.c (which instead mirrors the real clock Lichess reports). */
typedef struct
{
    double whiteSeconds;
    double blackSeconds;
    double incrementSeconds;
} ChessClock;

/* Sets up a clock with the same starting time for both sides. */
void clockInit(ChessClock *clock, double startSeconds, double incrementSeconds);

/* Returns a pointer to the given side's remaining time within the clock,
 * so callers can read or adjust it directly (e.g. to mirror a server's
 * authoritative clock instead of a locally-tracked one). */
double *clockTimeFor(ChessClock *clock, PieceColor side);

/* Subtracts elapsedSeconds from side's remaining time, then - only if that
 * didn't deplete it - adds the increment. Returns false if the side just
 * ran out of time (flagged), true otherwise. */
bool clockConsume(ChessClock *clock, PieceColor side, double elapsedSeconds);

/* True if side's remaining time is at or below zero. */
bool clockHasFlagged(ChessClock *clock, PieceColor side);

/* Formats seconds as "m:ss" (or "h:mm:ss" past an hour) into buf. */
void clockFormat(double seconds, char *buf, size_t bufSize);

#endif // TIMECONTROL_H
