#include "timecontrol.h"
#include <stdio.h>

void clockInit(ChessClock *clock, double startSeconds, double incrementSeconds)
{
    clock->whiteSeconds = startSeconds;
    clock->blackSeconds = startSeconds;
    clock->incrementSeconds = incrementSeconds;
}

double *clockTimeFor(ChessClock *clock, PieceColor side)
{
    return (side == WHITE) ? &clock->whiteSeconds : &clock->blackSeconds;
}

bool clockConsume(ChessClock *clock, PieceColor side, double elapsedSeconds)
{
    double *remaining = clockTimeFor(clock, side);
    *remaining -= elapsedSeconds;

    if (*remaining <= 0)
        return false;

    *remaining += clock->incrementSeconds;
    return true;
}

bool clockHasFlagged(ChessClock *clock, PieceColor side)
{
    return *clockTimeFor(clock, side) <= 0;
}

void clockFormat(double seconds, char *buf, size_t bufSize)
{
    if (seconds < 0)
        seconds = 0;

    int totalSeconds = (int)(seconds + 0.5); // round to the nearest second
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;

    if (hours > 0)
        snprintf(buf, bufSize, "%d:%02d:%02d", hours, minutes, secs);
    else
        snprintf(buf, bufSize, "%d:%02d", minutes, secs);
}
