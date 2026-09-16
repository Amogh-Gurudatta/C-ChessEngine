#include "uci.h"
#include "structs.h"
#include "game.h"
#include "ai.h"
#include "notation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define UCI_LINE_MAX 8192
#define UCI_STARTPOS_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

/* Every command line is tokenized with one ongoing strtok() sequence,
 * started in runUciLoop() and continued (via strtok(NULL, ...)) inside
 * these handlers rather than restarted - this project builds under strict
 * -std=c17, so the POSIX-only strtok_r isn't available, and plain strtok's
 * single shared cursor is fine here since parsing is strictly sequential
 * (no nested or concurrent tokenization of two different lines). */

/* "position [startpos | fen <fen>] [moves <m1> <m2> ...]" - firstToken is
 * the token right after "position" ("startpos" or "fen"), if any. */
static void uciHandlePosition(BoardState *board, char *firstToken)
{
    char *token = firstToken;
    if (token == NULL)
        return;

    if (!strcmp(token, "startpos"))
    {
        fenToBoard(UCI_STARTPOS_FEN, board);
        token = strtok(NULL, " \t");
    }
    else if (!strcmp(token, "fen"))
    {
        char fenBuf[FEN_MAX_LEN];
        fenBuf[0] = '\0';
        token = strtok(NULL, " \t");
        while (token != NULL && strcmp(token, "moves") != 0)
        {
            if (fenBuf[0] != '\0')
                strncat(fenBuf, " ", sizeof(fenBuf) - strlen(fenBuf) - 1);
            strncat(fenBuf, token, sizeof(fenBuf) - strlen(fenBuf) - 1);
            token = strtok(NULL, " \t");
        }
        if (!fenToBoard(fenBuf, board))
            return; // malformed FEN; leave the board as it was
    }
    else
    {
        return; // neither "startpos" nor "fen"; malformed command
    }

    if (token != NULL && !strcmp(token, "moves"))
    {
        token = strtok(NULL, " \t");
        while (token != NULL)
        {
            Move raw, resolved;
            if (parseLongAlgebraic(token, &raw) && resolveMove(board, raw, &resolved))
                makeMove(board, resolved);
            token = strtok(NULL, " \t");
        }
    }
}

/* "go [wtime <ms>] [btime <ms>] [winc <ms>] [binc <ms>] [movetime <ms>]
 *     [depth <n>] [...other tokens, read and ignored]"
 *
 * Priority mirrors what a real match runner actually sends: movetime (a
 * flat per-move budget) beats a real clock, which beats a bare depth
 * (searched uncapped, since a depth-only match expects the full depth
 * regardless of how long it takes), which beats the engine's own defaults
 * for a bare "go". Tokens this engine has no equivalent for - "infinite",
 * "ponder", "searchmoves", "nodes", "mate" - are read (so they don't get
 * misparsed as unknown keys) but otherwise have no effect; see docs/UCI.md. */
static void uciHandleGo(BoardState *board, char *firstToken)
{
    double wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
    int depth = -1;
    bool haveWtime = false, haveBtime = false;

    char *token = firstToken;
    while (token != NULL)
    {
        bool takesValue = !strcmp(token, "wtime") || !strcmp(token, "btime") ||
                           !strcmp(token, "winc") || !strcmp(token, "binc") ||
                           !strcmp(token, "movetime") || !strcmp(token, "depth");
        if (takesValue)
        {
            char *valueTok = strtok(NULL, " \t");
            if (valueTok == NULL)
                break;

            if (!strcmp(token, "wtime"))
            {
                wtime = atof(valueTok) / 1000.0;
                haveWtime = true;
            }
            else if (!strcmp(token, "btime"))
            {
                btime = atof(valueTok) / 1000.0;
                haveBtime = true;
            }
            else if (!strcmp(token, "winc"))
                winc = atof(valueTok) / 1000.0;
            else if (!strcmp(token, "binc"))
                binc = atof(valueTok) / 1000.0;
            else if (!strcmp(token, "movetime"))
                movetime = atof(valueTok) / 1000.0;
            else if (!strcmp(token, "depth"))
                depth = atoi(valueTok);
        }

        token = strtok(NULL, " \t");
    }

    int previousDepth = getSearchDepth();
    double previousTimeLimit = getSearchTimeLimit();

    if (depth >= 1)
        setSearchDepth(depth);

    Move best;
    if (movetime > 0)
    {
        setSearchTimeLimit(movetime);
        best = findBestMove(board);
    }
    else if (haveWtime && haveBtime)
    {
        double remaining = (board->currentPlayer == WHITE) ? wtime : btime;
        double increment = (board->currentPlayer == WHITE) ? winc : binc;
        best = findBestMoveTimed(board, remaining, increment);
    }
    else if (depth >= 1)
    {
        setSearchTimeLimit(0); // uncap: a fixed-depth match wants the full depth, however long it takes
        best = findBestMove(board);
    }
    else
    {
        best = findBestMove(board);
    }

    setSearchDepth(previousDepth);
    setSearchTimeLimit(previousTimeLimit);

    if (best.from.row == -1)
    {
        // No legal move (the position was already checkmate/stalemate) -
        // "0000" is UCI's conventional null move; there's no clean way to
        // say "no move" otherwise.
        printf("bestmove 0000\n");
    }
    else
    {
        char moveBuf[8];
        moveToLongAlgebraic(best, moveBuf, sizeof(moveBuf));
        printf("bestmove %s\n", moveBuf);
    }
}

void runUciLoop(void)
{
    // Force line buffering even when stdout is a pipe (the GUI's normal
    // case, not a terminal) - otherwise glibc fully buffers it and every
    // response above could sit unseen until the process exits.
    setvbuf(stdout, NULL, _IOLBF, 0);

    BoardState board;
    fenToBoard(UCI_STARTPOS_FEN, &board);

    char line[UCI_LINE_MAX];
    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';

        char *command = strtok(line, " \t");
        if (command == NULL)
            continue;

        char *rest = strtok(NULL, " \t"); // first token after the command, if any

        if (!strcmp(command, "uci"))
        {
            printf("id name C-ChessEngine\n");
            printf("id author Amogh Gurudatta\n");
            printf("uciok\n");
        }
        else if (!strcmp(command, "isready"))
        {
            printf("readyok\n");
        }
        else if (!strcmp(command, "ucinewgame"))
        {
            fenToBoard(UCI_STARTPOS_FEN, &board);
        }
        else if (!strcmp(command, "position"))
        {
            uciHandlePosition(&board, rest);
        }
        else if (!strcmp(command, "go"))
        {
            uciHandleGo(&board, rest);
        }
        else if (!strcmp(command, "quit"))
        {
            break;
        }
        // "stop", "debug", "setoption", "ponderhit", "register", and any
        // other unrecognized command are silently ignored, per the UCI
        // spec's own rule to ignore commands you don't understand. "stop"
        // in particular can't do anything useful here: the search is one
        // blocking call, so a "stop" sent mid-search can't even be read
        // until that call has already returned bestmove on its own.

        fflush(stdout);
    }
}
