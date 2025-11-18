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

// SIDE EFFECT: filters illegal moves
bool isMoveLegal(BoardState *board, Move m)
{
    MoveList list = {0};
    // generate legal moves
    list = generateAllLegalMoves(board);

    for (int i = 0; i < list.count; i++)
    {
        Move a = list.moves[i];
        if (a.from.row == m.from.row &&
            a.from.col == m.from.col &&
            a.to.row == m.to.row &&
            a.to.col == m.to.col)
            return true;
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

    while (1)
    {
        printBoard(&board);

        // Check whose turn
        if (board.currentPlayer == WHITE)
        {
            printf("\nYour move (e.g. e2e4 or 'save' or 'quit'): ");
            char input[32];
            scanf("%s", input);

            if (!strcmp(input, "quit"))
                break;
            if (!strcmp(input, "save"))
            {
                saveBoardToFile("board.txt", &board);
                printf("Saved.\n");
                continue;
            }

            Move m = parseMove(input);
            if (m.from.row == -1)
            {
                printf("Invalid format.\n");
                continue;
            }

            if (!isMoveLegal(&board, m))
            {
                printf("Illegal move.\n");
                continue;
            }

            makeMove(&board, m);
        }
        else
        {
            printf("\nAI thinking...\n");
            Move bm = findBestMove(&board);

            if (bm.from.row == -1)
            {
                printf("No legal moves. Game over.\n");
                break;
            }

            makeMove(&board, bm);
        }
    }

    printf("Exiting...\n");
    return 0;
}
