/**
 * @file uci.h
 * @brief A minimal UCI (Universal Chess Interface) front end, so the engine
 * can be driven by a standard chess GUI or match runner (e.g. cutechess-cli,
 * Arena, or the cutechess GUI) instead of this project's own REPL.
 *
 * See docs/UCI.md for exactly which commands are handled, which are
 * silently ignored, and the one real limitation (no true "stop" mid-search,
 * since the search is a single synchronous call rather than a background
 * thread).
 */

#ifndef UCI_H
#define UCI_H

/**
 * @brief Runs the UCI protocol loop: reads commands from stdin and writes
 * responses to stdout until "quit" is received or stdin closes. Owns its
 * own BoardState, entirely separate from main.c's local-play loop.
 */
void runUciLoop(void);

#endif // UCI_H
