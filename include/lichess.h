#ifndef LICHESS_H
#define LICHESS_H

#include <stdbool.h>
#include <stddef.h>

/* Reads LICHESS_API_TOKEN from the environment. Never logs/prints it. */
bool lichessGetToken(char *buf, size_t bufSize);

/* Plays a single live game on Lichess from the terminal, using the
 * Board API (https://lichess.org/api#tag/Board). The user must create
 * or accept the game on lichess.org first and supply its game ID.
 * Owns its own I/O loop; returns when the game ends or a fatal error
 * occurs, printing diagnostics to stdout/stderr as it goes.
 *
 * When botMode is true, the engine plays its own moves automatically
 * (via the clock-aware findBestMoveTimed, using the real wtime/btime/
 * winc/binc Lichess reports) instead of reading them from stdin. */
void playLichessGame(const char *gameId, bool botMode);

#endif // LICHESS_H
