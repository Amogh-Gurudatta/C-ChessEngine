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
/* MAIN LOOP                                                                  */
/* ========================================================================== */

int main(int argc, char *argv[])
{
#ifdef LICHESS_ENABLED
    if (argc >= 3 && !strcmp(argv[1], "--lichess"))
    {
        playLichessGame(argv[2]);
        return 0;
    }
#endif

    BoardState board;

    // Move record for PGN export / the "moves" command.
    char sanLog[1024][SAN_MAX_LEN];
    int sanCount = 0;
    const char *result = "*";

    // 1. Game Initialization
    // Try to load a saved game, otherwise a position given via --fen, otherwise
    // fall back to the standard chess starting position.
    if (argc >= 3 && !strcmp(argv[1], "--fen") && fenToBoard(argv[2], &board))
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
                        printf("Position loaded.\n");
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

            // Parse the move (long algebraic or SAN) and resolve it against legality.
            Move finalMove;
            if (parseUserMove(&board, input, &finalMove))
            {
                if (sanCount < 1024)
                    moveToSan(&board, finalMove, sanLog[sanCount], SAN_MAX_LEN);
                makeMove(&board, finalMove);
                if (sanCount < 1024)
                    sanCount++;
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
        }
    }

    if (sanCount > 0)
    {
        if (exportPgn("game.pgn", sanLog, sanCount, "Player", "AI", result))
            printf("Game record saved to game.pgn\n");
    }

    return 0;
}