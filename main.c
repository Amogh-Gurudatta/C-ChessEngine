/*
 * ======================================================================================
 * File: main.c
 * Description: The main entry point for the Console Chess Engine.
 *
 * Responsibilities:
 * 1. Game Loop: Manages the flow between Human (White) and AI (Black).
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

#include "structs.h"
#include "fileio.h"
#include "game.h"
#include "ai.h"
#include "eval.h"
#include "notation.h"
#ifdef LICHESS_ENABLED
#include "lichess.h"
#endif

/* ========================================================================== */
/* VISUALIZATION HELPERS                                                      */
/* ========================================================================== */

/**
 * @brief Prints the current board state to the console.
 * Includes Rank numbers (1-8) and File letters (a-h).
 */
void printBoard(BoardState *board)
{
    printf("\n   +-----------------+\n");
    // Iterate Rows from 0 (Rank 8) to 7 (Rank 1)
    for (int r = 0; r < 8; r++)
    {
        printf(" %d | ", 8 - r); // Print Rank Number
        for (int c = 0; c < 8; c++)
        {
            printf("%c ", pieceToChar(board->squares[r][c]));
        }
        printf("|\n");
    }
    printf("   +-----------------+\n");
    printf("     a b c d e f g h\n");

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
/* MAIN LOOP                                                                  */
/* ========================================================================== */

int main(int argc, char *argv[])
{
    const char *depthArg = findArgValue(argc, argv, "--depth");
    if (depthArg != NULL)
    {
        int depth = atoi(depthArg);
        if (depth >= 1)
            setSearchDepth(depth);
        else
            printf("Ignoring invalid --depth value; must be a positive integer.\n");
    }

#ifdef LICHESS_ENABLED
    const char *lichessGameId = findArgValue(argc, argv, "--lichess");
    if (lichessGameId != NULL)
    {
        playLichessGame(lichessGameId);
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

    // 2. The Game Loop
    while (1)
    {
        printBoard(&board);

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
                // King is in check and has no moves => Checkmate
                printf("\n============================\n");
                printf("CHECKMATE! %s wins.\n", board.currentPlayer == WHITE ? "Black (AI)" : "White (You)");
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
        if (board.currentPlayer == WHITE)
        {
            // --- HUMAN TURN (WHITE) ---
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
                if (exportPgn("game.pgn", sanLog, sanCount, "Player", "AI", "*"))
                    printf("Game record saved to game.pgn\n");
                continue;
            }
            if (!strcmp(input, "moves"))
            {
                printMoveLog(sanLog, sanCount);
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

            // Parse the move (long algebraic or SAN) and resolve it against legality.
            Move finalMove;
            if (parseUserMove(&board, input, &finalMove))
            {
                if (sanCount < 1024)
                    moveToSan(&board, finalMove, sanLog[sanCount], SAN_MAX_LEN);
                makeMove(&board, finalMove);
                if (sanCount < 1024)
                    sanCount++;
                if (positionHistoryCount < MAX_POSITION_HISTORY)
                    boardToPositionKey(&board, positionHistory[positionHistoryCount++], FEN_MAX_LEN);
                if (fenHistoryCount < MAX_POSITION_HISTORY)
                    boardToFen(&board, fenHistory[fenHistoryCount++], FEN_MAX_LEN);
            }
            else
            {
                printf("Illegal move. Please try again.\n");
            }
        }
        else
        {
            // --- AI TURN (BLACK) ---
            printf("\nAI is thinking...\n");

            // AI finds the best move
            Move best = findBestMove(&board);

            // Sanity check: Should never happen if game-over logic above is correct
            if (best.from.row == -1)
            {
                printf("AI resigns (Error or Mate).\n");
                break;
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
        }
    }

    if (sanCount > 0)
    {
        if (exportPgn("game.pgn", sanLog, sanCount, "Player", "AI", result))
            printf("Game record saved to game.pgn\n");
    }

    return 0;
}