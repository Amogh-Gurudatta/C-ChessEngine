#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "structs.h"
#include "fileio.h"
#include "game.h"
#include "ai.h"

// print board nicely
void printBoard(BoardState *board)
{
    printf("\n   +-----------------+\n");
    for (int r = 0; r < 8; r++)
    {
        printf(" %d | ", 8 - r);
        for (int c = 0; c < 8; c++)
        {
            printf("%c ", pieceToChar(board->squares[r][c]));
        }
        printf("|\n");
    }
    printf("   +-----------------+\n");
    printf("     a b c d e f g h\n");
    printf("Side to move: %s\n",
           board->currentPlayer == WHITE ? "White" : "Black");
}

// convert algebraic like "e2e4"
Move parseMove(char *s)
{
    Move m;
    m.flag = MOVE_NORMAL;
    m.promotion = EMPTY;

    if (strlen(s) < 4)
    {
        m.from.row = -1;
        return m;
    }

    int fromC = s[0] - 'a';
    int fromR = 8 - (s[1] - '0');
    int toC = s[2] - 'a';
    int toR = 8 - (s[3] - '0');

    m.from = (Position){fromR, fromC};
    m.to = (Position){toR, toC};

    // Check promotion
    if (strlen(s) == 5)
    {
        switch (tolower(s[4]))
        {
        case 'q':
            m.promotion = QUEEN;
            break;
        case 'r':
            m.promotion = ROOK;
            break;
        case 'b':
            m.promotion = BISHOP;
            break;
        case 'n':
            m.promotion = KNIGHT;
            break;
        }
        m.flag = MOVE_PROMOTION;
    }

    return m;
}

// Matches user input to a valid legal move from the engine.
// Returns true if found, and populates *resolvedMove with the correct flags.
bool resolveMove(BoardState *board, Move inputMove, Move *resolvedMove)
{
    MoveList list = generateAllLegalMoves(board);

    for (int i = 0; i < list.count; i++)
    {
        Move m = list.moves[i];

        // 1. Check if coordinates match (From -> To)
        if (m.from.row == inputMove.from.row && m.from.col == inputMove.from.col &&
            m.to.row == inputMove.to.row && m.to.col == inputMove.to.col)
        {
            // 2. Handle Promotion Logic
            if (m.flag == MOVE_PROMOTION)
            {
                // Case A: User requested a specific promotion (e.g., "a7a8r")
                if (inputMove.flag == MOVE_PROMOTION)
                {
                    if (m.promotion == inputMove.promotion)
                    {
                        *resolvedMove = m;
                        return true;
                    }
                }
                // Case B: User typed only coordinates (e.g., "a7a8"). Default to QUEEN.
                else if (m.promotion == QUEEN)
                {
                    *resolvedMove = m;
                    return true;
                }
            }
            else
            {
                // 3. Normal Moves (Castling, En Passant, Regular)
                // We accept the engine's version because it has the correct flags.
                *resolvedMove = m;
                return true;
            }
        }
    }

    return false;
}

int main()
{
    BoardState board;

    // Try to load initial board, else set default
    if (!loadBoardFromFile("board.txt", &board))
    {
        printf("No board.txt found. Loading standard start.\n");

        // Standard chess position
        const char *start[8] = {
            "rnbqkbnr",
            "pppppppp",
            "........",
            "........",
            "........",
            "........",
            "PPPPPPPP",
            "RNBQKBNR"};

        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                board.squares[r][c] = charToPiece(start[r][c]);

        board.currentPlayer = WHITE;
        board.castling = (CastlingRights){1, 1, 1, 1};
        board.enPassantTarget = (Position){-1, -1};
        board.halfmoveClock = 0;
        board.fullmoveNumber = 1;
    }

    // [Inside main()]
    while (1)
    {
        printBoard(&board);

        if (board.currentPlayer == WHITE)
        {
            printf("\nYour move (e.g. e2e4, a7a8q, or 'quit'): ");
            char input[32];
            scanf("%s", input); //

            if (!strcmp(input, "quit"))
                break;
            if (!strcmp(input, "save"))
            {
                saveBoardToFile("board.txt", &board);
                printf("Saved.\n");
                continue;
            }

            Move parsed = parseMove(input); //
            if (parsed.from.row == -1)
            {
                printf("Invalid format.\n");
                continue;
            }

            // --- CHANGED SECTION START ---
            Move finalMove;
            if (!resolveMove(&board, parsed, &finalMove))
            {
                printf("Illegal move.\n");
                continue;
            }

            makeMove(&board, finalMove);
            // --- CHANGED SECTION END ---
        }
        else
        {
            printf("\nAI thinking...\n");
            Move bm = findBestMove(&board);

            if (bm.from.row == -1)
            {
                printf("Game over (Checkmate or Stalemate).\n");
                break;
            }

            // Print what the AI actually played
            char moveStr[6];
            // Simple helper to print AI move
            moveStr[0] = 'a' + bm.from.col;
            moveStr[1] = '8' - bm.from.row;
            moveStr[2] = 'a' + bm.to.col;
            moveStr[3] = '8' - bm.to.row;
            moveStr[4] = '\0';
            // Add promotion char if needed
            if (bm.flag == MOVE_PROMOTION)
            {
                char p = 'q';
                if (bm.promotion == ROOK)
                    p = 'r';
                if (bm.promotion == BISHOP)
                    p = 'b';
                if (bm.promotion == KNIGHT)
                    p = 'n';
                moveStr[4] = p;
                moveStr[5] = '\0';
            }
            printf("AI plays: %s\n", moveStr);

            makeMove(&board, bm);
        }
    }

    printf("Exiting...\n");
    return 0;
}
