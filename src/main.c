/*
 * ======================================================================================
 * File: main.c
 * Description: The main entry point for the Console Chess Engine.
 *
 * Responsibilities:
 * 1. Game Loop: Manages the flow between the Human and the AI, whichever
 *    color each is playing (see --side).
 * 2. Input Parsing: Converts Algebraic Notation ("e2e4") into engine coordinates.
 * 3. Output: Displays the board and game status.
 * 4. Game Over Detection: Checks for Checkmate/Stalemate at the start of every turn.
 * ======================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>

#include "structs.h"
#include "fileio.h"
#include "game.h"
#include "ai.h"
#include "eval.h"
#include "notation.h"
#include "timecontrol.h"
#include "uci.h"
#ifdef LICHESS_ENABLED
#include "lichess.h"
#endif

/* ========================================================================== */
/* VISUALIZATION HELPERS                                                      */
/* ========================================================================== */

/**
 * @brief Maps a piece to a Unicode chess glyph (e.g. white king -> "♔").
 * White pieces use the "outline" glyphs and black pieces the "filled" ones,
 * the standard convention for rendering chess in Unicode.
 */
static const char *pieceToGlyph(Piece p)
{
    switch (p.type)
    {
    case PAWN:
        return (p.color == WHITE) ? "♙" : "♟";
    case KNIGHT:
        return (p.color == WHITE) ? "♘" : "♞";
    case BISHOP:
        return (p.color == WHITE) ? "♗" : "♝";
    case ROOK:
        return (p.color == WHITE) ? "♖" : "♜";
    case QUEEN:
        return (p.color == WHITE) ? "♕" : "♛";
    case KING:
        return (p.color == WHITE) ? "♔" : "♚";
    default:
        return "·"; // middle dot for an empty square
    }
}

/**
 * @brief Whether the board should be printed with ANSI background/foreground
 * colors. Disabled when stdout isn't a real terminal (e.g. piped to a file
 * or another process) or when the NO_COLOR convention (https://no-color.org)
 * is requested, so colored escape codes never leak into redirected output.
 */
static bool boardShouldUseColor(void)
{
    static bool checked = false;
    static bool useColor = false;
    if (!checked)
    {
        useColor = (getenv("NO_COLOR") == NULL) && isatty(STDOUT_FILENO);
        checked = true;
    }
    return useColor;
}

/**
 * @brief Prints the current board state to the console.
 * Includes Rank numbers (1-8) and File letters (a-h). Uses Unicode chess
 * glyphs, with alternating light/dark square colors when the terminal
 * supports it (see boardShouldUseColor).
 *
 * @param board The position to display.
 * @param perspective Whose side of the board to display it from: WHITE
 * shows it the traditional way (rank 8 at the top); BLACK shows it rotated
 * 180 degrees (rank 1 at the top, files h-a left to right), matching how
 * it would look sitting across the board from White - standard practice
 * when a human is playing Black, so the square someone reaches for is
 * physically in the direction they'd expect.
 */
void printBoard(BoardState *board, PieceColor perspective)
{
    bool color = boardShouldUseColor();
    bool flipped = (perspective == BLACK);

    printf("\n");
    for (int i = 0; i < 8; i++)
    {
        int r = flipped ? (7 - i) : i;
        printf(" %d ", 8 - r); // Print Rank Number
        for (int j = 0; j < 8; j++)
        {
            int c = flipped ? (7 - j) : j;
            Piece p = board->squares[r][c];
            const char *glyph = pieceToGlyph(p);

            if (color)
            {
                bool lightSquare = ((r + c) % 2 == 0); // a property of the square itself, not the viewing angle
                const char *bg = lightSquare ? "\033[48;5;180m" : "\033[48;5;94m";
                const char *fg = (p.color == WHITE) ? "\033[97m" : "\033[30m";
                printf("%s%s %s \033[0m", bg, fg, glyph);
            }
            else
            {
                printf(" %s ", glyph);
            }
        }
        printf("\n");
    }
    printf(flipped ? "    h  g  f  e  d  c  b  a\n" : "    a  b  c  d  e  f  g  h\n");

    printf("Side to move: %s\n", board->currentPlayer == WHITE ? "White" : "Black");
}

/* ========================================================================== */
/* MOVE LOG / GAME RECORD HELPERS                                             */
/* ========================================================================== */

/**
 * @brief Prints the accumulated SAN move log in "1. e4 e5 2. Nf3 ..." form.
 */
void printMoveLog(char sanLog[][SAN_MAX_LEN], int sanCount)
{
    for (int i = 0; i < sanCount; i++)
    {
        if (i % 2 == 0)
            printf("%d. ", i / 2 + 1);
        printf("%s ", sanLog[i]);
    }
    printf("\n");
}

/* ========================================================================== */
/* THREEFOLD REPETITION HELPERS                                               */
/* ========================================================================== */

#define MAX_POSITION_HISTORY 1024

/**
 * @brief Counts how many times a position (by its repetition key) has
 * already occurred in the game's position history.
 */
int countPositionOccurrences(char history[][FEN_MAX_LEN], int historyCount, const char *key)
{
    int count = 0;
    for (int i = 0; i < historyCount; i++)
    {
        if (!strcmp(history[i], key))
            count++;
    }
    return count;
}

/**
 * @brief Finds the value following a flag anywhere in argv (e.g. "--depth 4"),
 * so it can be combined freely with the other single-flag options below.
 */
static const char *findArgValue(int argc, char *argv[], const char *flag)
{
    for (int i = 1; i < argc - 1; i++)
    {
        if (!strcmp(argv[i], flag))
            return argv[i + 1];
    }
    return NULL;
}

/* ========================================================================== */
/* CHESS CLOCK HELPERS                                                        */
/* ========================================================================== */

/**
 * @brief Parses a "--clock" spec like "5+3" (5 minutes + 3s increment) or
 * plain "10" (10 minutes, no increment) into seconds.
 */
static bool parseClockSpec(const char *spec, double *outStartSeconds, double *outIncrementSeconds)
{
    double minutes = atof(spec);
    if (minutes <= 0)
        return false;

    const char *plusPos = strchr(spec, '+');
    *outStartSeconds = minutes * 60.0;
    *outIncrementSeconds = (plusPos != NULL) ? atof(plusPos + 1) : 0.0;
    return true;
}

/**
 * @brief Prints both sides' remaining clock time under the board.
 */
static void printClocks(ChessClock *matchClock)
{
    char whiteBuf[16], blackBuf[16];
    clockFormat(matchClock->whiteSeconds, whiteBuf, sizeof(whiteBuf));
    clockFormat(matchClock->blackSeconds, blackBuf, sizeof(blackBuf));
    printf("Clock - White: %s   Black: %s\n", whiteBuf, blackBuf);
}

/* ========================================================================== */
/* SIDE-SELECTION HELPERS                                                     */
/* ========================================================================== */

static const char *colorName(PieceColor color)
{
    return (color == WHITE) ? "White" : "Black";
}

/** "You" if color is the human's side, "AI" otherwise - for messages like
 * "Black (AI) wins" that need to say who's who regardless of which side
 * the human chose to play. */
static const char *roleFor(PieceColor color, PieceColor humanColor)
{
    return (color == humanColor) ? "You" : "AI";
}

/* ========================================================================== */
/* MAIN LOOP                                                                  */
/* ========================================================================== */

int main(int argc, char *argv[])
{
    // Checked before anything else, and before any other flag prints a
    // single byte to stdout: a UCI-speaking GUI/match runner (e.g.
    // cutechess-cli) expects the very first thing on stdout to be part of
    // the UCI handshake, not this project's own banner/prompts. A plain
    // boolean flag like "--bot", so it's checked by scanning argv directly
    // rather than via findArgValue(), which requires a following value and
    // would miss "--uci" as the very last (or only) argument.
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--uci"))
        {
            runUciLoop();
            return 0;
        }
    }

    const char *depthArg = findArgValue(argc, argv, "--depth");
    if (depthArg != NULL)
    {
        int depth = atoi(depthArg);
        if (depth >= 1)
            setSearchDepth(depth);
        else
            printf("Ignoring invalid --depth value; must be a positive integer.\n");
    }

    const char *timeArg = findArgValue(argc, argv, "--time");
    if (timeArg != NULL)
    {
        double seconds = atof(timeArg);
        if (seconds > 0)
            setSearchTimeLimit(seconds);
        else
            printf("Ignoring invalid --time value; must be a positive number of seconds.\n");
    }

    // Which color the human plays; the AI takes the other one. Only
    // meaningful for local play - Lichess assigns your color per-game.
    PieceColor humanColor = WHITE;
    const char *sideArg = findArgValue(argc, argv, "--side");
    if (sideArg != NULL)
    {
        char firstChar = (char)tolower((unsigned char)sideArg[0]);
        humanColor = (firstChar == 'b') ? BLACK : WHITE;
    }
    PieceColor aiColor = (humanColor == WHITE) ? BLACK : WHITE;

    bool clockEnabled = false;
    ChessClock matchClock;
    const char *clockArg = findArgValue(argc, argv, "--clock");
    if (clockArg != NULL)
    {
        double startSeconds, incrementSeconds;
        if (parseClockSpec(clockArg, &startSeconds, &incrementSeconds))
        {
            clockInit(&matchClock, startSeconds, incrementSeconds);
            clockEnabled = true;
        }
        else
        {
            printf("Ignoring invalid --clock value; expected e.g. \"5+3\" or \"10\".\n");
        }
    }

#ifdef LICHESS_ENABLED
    const char *lichessGameId = findArgValue(argc, argv, "--lichess");
    if (lichessGameId != NULL)
    {
        bool botMode = false;
        for (int i = 1; i < argc; i++)
        {
            if (!strcmp(argv[i], "--bot"))
                botMode = true;
        }
        playLichessGame(lichessGameId, botMode);
        return 0;
    }
#endif

    BoardState board;

    // Move record for PGN export / the "moves" command.
    char sanLog[1024][SAN_MAX_LEN];
    int sanCount = 0;
    const char *result = "*";

    // Position history for threefold-repetition detection.
    char positionHistory[MAX_POSITION_HISTORY][FEN_MAX_LEN];
    int positionHistoryCount = 0;

    // Full FEN snapshot after every move, so "undo" can roll the board back
    // without depending on game.c's internal (and loadfen-oblivious) undo stack.
    char fenHistory[MAX_POSITION_HISTORY][FEN_MAX_LEN];
    int fenHistoryCount = 0;

    // 1. Game Initialization
    // Try to load a saved game, otherwise a position given via --fen, otherwise
    // fall back to the standard chess starting position.
    const char *fenArg = findArgValue(argc, argv, "--fen");
    if (fenArg != NULL && fenToBoard(fenArg, &board))
    {
        printf("Starting from given FEN.\n");
    }
    else if (!loadBoardFromFile("board.txt", &board))
    {
        printf("Starting new game.\n");
        const char *start[8] = {
            "rnbqkbnr", "pppppppp", "........", "........",
            "........", "........", "PPPPPPPP", "RNBQKBNR"};
        // Parse the initial strings into the board array
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                board.squares[r][c] = charToPiece(start[r][c]);

        // Set initial state
        board.currentPlayer = WHITE;
        board.castling = (CastlingRights){1, 1, 1, 1};
        board.enPassantTarget = (Position){-1, -1};
        board.halfmoveClock = 0;
        board.fullmoveNumber = 1;
    }

    boardToPositionKey(&board, positionHistory[positionHistoryCount++], FEN_MAX_LEN);
    boardToFen(&board, fenHistory[fenHistoryCount++], FEN_MAX_LEN);

    // Wall-clock timestamp for whichever side's turn is currently running;
    // only reset when a move actually completes (not on every command).
    time_t turnStartTime = time(NULL);

    // 2. The Game Loop
    while (1)
    {
        printBoard(&board, humanColor);
        if (clockEnabled)
            printClocks(&matchClock);

        // ---------------------------------------------------------
        // STEP 1: CHECK GAME OVER CONDITIONS
        // ---------------------------------------------------------
        // We generate all legal moves for the *current* player.
        // If count is 0, the game is over (Checkmate or Stalemate).
        MoveList moves = generateAllLegalMoves(&board);

        if (moves.count == 0)
        {
            if (isKingInCheck(&board, board.currentPlayer))
            {
                // King is in check and has no moves => Checkmate. The
                // winner is whichever side isn't the one that's mated.
                PieceColor winner = (board.currentPlayer == WHITE) ? BLACK : WHITE;
                printf("\n============================\n");
                printf("CHECKMATE! %s (%s) wins.\n", colorName(winner), roleFor(winner, humanColor));
                printf("============================\n");
                result = (board.currentPlayer == WHITE) ? "0-1" : "1-0";
            }
            else
            {
                // King is NOT in check but has no moves => Stalemate
                printf("\n============================\n");
                printf("STALEMATE! The game is a draw.\n");
                printf("============================\n");
                result = "1/2-1/2";
            }

            // Deleteing saved board state, if it exists
            remove("board.txt");

            break; // Terminate loop
        }
        else if (board.halfmoveClock >= 100)
        {
            // 50 full moves (100 half-moves) without a capture or pawn move.
            printf("\n============================\n");
            printf("DRAW! 50-move rule.\n");
            printf("============================\n");
            result = "1/2-1/2";
            remove("board.txt");
            break;
        }
        else if (isInsufficientMaterial(&board))
        {
            printf("\n============================\n");
            printf("DRAW! Insufficient material.\n");
            printf("============================\n");
            result = "1/2-1/2";
            remove("board.txt");
            break;
        }
        else
        {
            char currentKey[FEN_MAX_LEN];
            boardToPositionKey(&board, currentKey, sizeof(currentKey));
            if (countPositionOccurrences(positionHistory, positionHistoryCount, currentKey) >= 3)
            {
                printf("\n============================\n");
                printf("DRAW! Threefold repetition.\n");
                printf("============================\n");
                result = "1/2-1/2";
                remove("board.txt");
                break;
            }
        }

        // ---------------------------------------------------------
        // STEP 2: EXECUTE TURNS
        // ---------------------------------------------------------
        if (board.currentPlayer == humanColor)
        {
            // --- HUMAN TURN ---
            printf("\nYour move (e.g. e2e4, e4, Nf3, quit): ");
            char input[32];

            // Read input safely
            if (scanf("%s", input) != 1)
                break;

            // Check Commands
            if (!strcmp(input, "quit"))
                break;
            if (!strcmp(input, "save"))
            {
                saveBoardToFile("board.txt", &board);
                printf("Game saved.\n");
                continue; // Restart loop to let user keep playing
            }
            if (!strcmp(input, "fen"))
            {
                char fenBuf[FEN_MAX_LEN];
                if (boardToFen(&board, fenBuf, sizeof(fenBuf)))
                    printf("%s\n", fenBuf);
                continue;
            }
            if (!strcmp(input, "loadfen"))
            {
                // Consume the rest of the current line before reading a fresh one.
                int ch;
                while ((ch = getchar()) != '\n' && ch != EOF)
                {
                }

                printf("Enter FEN string: ");
                char fenLine[FEN_MAX_LEN];
                if (fgets(fenLine, sizeof(fenLine), stdin))
                {
                    size_t flen = strlen(fenLine);
                    if (flen > 0 && fenLine[flen - 1] == '\n')
                        fenLine[flen - 1] = '\0';

                    if (fenToBoard(fenLine, &board))
                    {
                        // A manually loaded position starts a fresh repetition,
                        // undo, and move-log history; moves from before don't
                        // belong to it, and sanCount must stay in lockstep with
                        // fenHistoryCount/positionHistoryCount for "undo" to work.
                        sanCount = 0;
                        positionHistoryCount = 0;
                        boardToPositionKey(&board, positionHistory[positionHistoryCount++], FEN_MAX_LEN);
                        fenHistoryCount = 0;
                        boardToFen(&board, fenHistory[fenHistoryCount++], FEN_MAX_LEN);
                        printf("Position loaded.\n");
                    }
                    else
                        printf("Invalid FEN string.\n");
                }
                continue;
            }
            if (!strcmp(input, "pgn"))
            {
                const char *whiteName = (humanColor == WHITE) ? "Player" : "AI";
                const char *blackName = (humanColor == WHITE) ? "AI" : "Player";
                if (exportPgn("game.pgn", sanLog, sanCount, whiteName, blackName, "*"))
                    printf("Game record saved to game.pgn\n");
                continue;
            }
            if (!strcmp(input, "moves"))
            {
                printMoveLog(sanLog, sanCount);
                continue;
            }
            if (!strcmp(input, "resign"))
            {
                printf("\n============================\n");
                printf("You resigned. %s (AI) wins.\n", colorName(aiColor));
                printf("============================\n");
                result = (aiColor == WHITE) ? "1-0" : "0-1";
                remove("board.txt");
                break;
            }
            if (!strcmp(input, "draw"))
            {
                // evaluateBoard() is from White's perspective (positive favors
                // White); the AI's own advantage is that score as seen from
                // its own color. It accepts a draw offer unless it's clearly
                // ahead - a simple stand-in for real draw-offer negotiation.
                int aiAdvantage = (aiColor == WHITE) ? evaluateBoard(&board) : -evaluateBoard(&board);
                if (aiAdvantage < 150)
                {
                    printf("\n============================\n");
                    printf("The AI accepts your draw offer.\n");
                    printf("============================\n");
                    result = "1/2-1/2";
                    remove("board.txt");
                    break;
                }
                else
                {
                    printf("The AI declines your draw offer - it likes its position too much.\n");
                }
                continue;
            }
            if (!strcmp(input, "undo"))
            {
                // fenHistory[0] is the starting/checkpoint position, so that
                // many moves are already "used up" and can't be undone.
                int available = fenHistoryCount - 1;
                int toUndo = (available >= 2) ? 2 : available;

                if (toUndo == 0)
                {
                    printf("Nothing to undo.\n");
                }
                else
                {
                    fenHistoryCount -= toUndo;
                    sanCount -= toUndo;
                    positionHistoryCount -= toUndo;
                    fenToBoard(fenHistory[fenHistoryCount - 1], &board);
                    printf("Undid %d move(s). It's your move again.\n", toUndo);
                }
                continue;
            }
            if (!strcmp(input, "depth"))
            {
                // Consume the rest of the current line before reading a fresh one.
                int ch;
                while ((ch = getchar()) != '\n' && ch != EOF)
                {
                }

                char line[32];
                printf("Current search depth: %d. Enter a new depth (or press Enter to keep it): ",
                       getSearchDepth());
                if (fgets(line, sizeof(line), stdin) && line[0] != '\n')
                {
                    int depth = atoi(line);
                    if (depth >= 1)
                    {
                        setSearchDepth(depth);
                        printf("Search depth set to %d.\n", depth);
                    }
                    else
                    {
                        printf("Please enter a positive integer.\n");
                    }
                }
                continue;
            }
            if (!strcmp(input, "time"))
            {
                // Consume the rest of the current line before reading a fresh one.
                int ch;
                while ((ch = getchar()) != '\n' && ch != EOF)
                {
                }

                char line[32];
                double currentLimit = getSearchTimeLimit();
                if (currentLimit > 0)
                    printf("Current search time cap: %.1fs. Enter a new value in seconds, "
                            "0 to disable it, or press Enter to keep it: ",
                            currentLimit);
                else
                    printf("Search time cap is disabled (searching purely by depth). "
                            "Enter a value in seconds, or press Enter to keep it disabled: ");

                if (fgets(line, sizeof(line), stdin) && line[0] != '\n')
                {
                    double seconds = atof(line);
                    setSearchTimeLimit(seconds);
                    if (seconds > 0)
                        printf("Search time cap set to %.1fs.\n", seconds);
                    else
                        printf("Search time cap disabled.\n");
                }
                continue;
            }

            // Parse the move (long algebraic or SAN) and resolve it against legality.
            Move finalMove;
            if (parseUserMove(&board, input, &finalMove))
            {
                // Check the clock BEFORE committing the move: if time had
                // already run out by the moment this move arrived, it
                // doesn't count - the game is simply lost on time, exactly
                // like a physical clock's flag falling before you press it.
                if (clockEnabled)
                {
                    double elapsed = difftime(time(NULL), turnStartTime);
                    if (!clockConsume(&matchClock, humanColor, elapsed))
                    {
                        printf("\n============================\n");
                        printf("TIME! You ran out of time. %s (AI) wins.\n", colorName(aiColor));
                        printf("============================\n");
                        result = (aiColor == WHITE) ? "1-0" : "0-1";
                        remove("board.txt");
                        break;
                    }
                }

                if (sanCount < 1024)
                    moveToSan(&board, finalMove, sanLog[sanCount], SAN_MAX_LEN);
                makeMove(&board, finalMove);
                if (sanCount < 1024)
                    sanCount++;
                if (positionHistoryCount < MAX_POSITION_HISTORY)
                    boardToPositionKey(&board, positionHistory[positionHistoryCount++], FEN_MAX_LEN);
                if (fenHistoryCount < MAX_POSITION_HISTORY)
                    boardToFen(&board, fenHistory[fenHistoryCount++], FEN_MAX_LEN);

                turnStartTime = time(NULL);
            }
            else
            {
                printf("Illegal move. Please try again.\n");
            }
        }
        else
        {
            // --- AI TURN ---
            printf("\nAI is thinking...\n");

            // AI finds the best move: clock-aware (budgets its own thinking
            // time from the real clock) when a clock is running, or a plain
            // fixed-depth/fixed-cap search otherwise.
            Move best = clockEnabled
                             ? findBestMoveTimed(&board, *clockTimeFor(&matchClock, aiColor), matchClock.incrementSeconds)
                             : findBestMove(&board);

            // Sanity check: Should never happen if game-over logic above is correct
            if (best.from.row == -1)
            {
                printf("AI resigns (Error or Mate).\n");
                break;
            }

            // Check the clock BEFORE committing the move, for the same
            // reason as the human branch above: a move found after the
            // flag has already fallen doesn't count.
            if (clockEnabled)
            {
                double elapsed = difftime(time(NULL), turnStartTime);
                if (!clockConsume(&matchClock, aiColor, elapsed))
                {
                    printf("\n============================\n");
                    printf("TIME! The AI ran out of time. %s (You) wins.\n", colorName(humanColor));
                    printf("============================\n");
                    result = (humanColor == WHITE) ? "1-0" : "0-1";
                    remove("board.txt");
                    break;
                }
            }

            char sanBuf[SAN_MAX_LEN];
            moveToSan(&board, best, sanBuf, sizeof(sanBuf));
            printf("AI plays: %s\n", sanBuf);

            // Execute AI move
            makeMove(&board, best);

            if (sanCount < 1024)
                strcpy(sanLog[sanCount++], sanBuf);
            if (positionHistoryCount < MAX_POSITION_HISTORY)
                boardToPositionKey(&board, positionHistory[positionHistoryCount++], FEN_MAX_LEN);
            if (fenHistoryCount < MAX_POSITION_HISTORY)
                boardToFen(&board, fenHistory[fenHistoryCount++], FEN_MAX_LEN);

            turnStartTime = time(NULL);
        }
    }

    if (sanCount > 0)
    {
        const char *whiteName = (humanColor == WHITE) ? "Player" : "AI";
        const char *blackName = (humanColor == WHITE) ? "AI" : "Player";
        if (exportPgn("game.pgn", sanLog, sanCount, whiteName, blackName, result))
            printf("Game record saved to game.pgn\n");
    }

    return 0;
}