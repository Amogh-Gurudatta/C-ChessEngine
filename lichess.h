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
 * occurs, printing diagnostics to stdout/stderr as it goes. */
void playLichessGame(const char *gameId);

#endif // LICHESS_H
