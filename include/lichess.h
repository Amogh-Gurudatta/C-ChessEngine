/**
 * @file lichess.h
 * @brief Lichess Board API client (https://lichess.org/api#tag/Board):
 * play a real, live Lichess game from the terminal, either by relaying
 * moves you type or (in bot mode) letting the engine play automatically.
 *
 * Only compiled in when the project is built with `make LICHESS=1` (see
 * the Makefile) - this is the one module with an external dependency
 * (libcurl). See docs/ONLINE_PLAY.md for the protocol details, the
 * lightweight JSON extraction approach, and why chess.com isn't supported.
 */

#ifndef LICHESS_H
#define LICHESS_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Reads a Lichess personal API token from the LICHESS_API_TOKEN
 * environment variable. Never logs or prints the token itself.
 *
 * @param buf Destination buffer.
 * @param bufSize Size of buf.
 * @return true if the environment variable was set and non-empty.
 */
bool lichessGetToken(char *buf, size_t bufSize);

/**
 * @brief Plays a single live game on Lichess from the terminal, using the
 * Board API. The user must create or accept the game on lichess.org first
 * and supply its game ID. Owns its own I/O loop; returns when the game
 * ends or a fatal error occurs, printing diagnostics to stdout/stderr as
 * it goes.
 *
 * @param gameId The Lichess game ID to connect to (from the game's URL).
 * @param botMode When true, the engine plays its own moves automatically
 * (via the clock-aware ai.h's findBestMoveTimed(), using the real
 * wtime/btime/winc/binc Lichess reports for the game) instead of reading
 * them from stdin.
 */
void playLichessGame(const char *gameId, bool botMode);

#endif // LICHESS_H
