#include "fileio.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// -------------------- Helpers --------------------

char pieceToChar(Piece p)
{
    if (p.type == EMPTY)
        return '.';

    char c;
    switch (p.type)
    {
    case PAWN:
        c = 'p';
        break;
    case KNIGHT:
        c = 'n';
        break;
    case BISHOP:
        c = 'b';
        break;
    case ROOK:
        c = 'r';
        break;
    case QUEEN:
        c = 'q';
        break;
    case KING:
        c = 'k';
        break;
    default:
        c = '?';
    }
    return (p.color == WHITE) ? toupper(c) : c;
}

Piece charToPiece(char c)
{
    Piece p = {EMPTY, NO_COLOR};
    if (c == '.' || c == ' ')
        return p;

    PieceColor color = isupper(c) ? WHITE : BLACK;
    char lower = tolower(c);

    PieceType type;
    switch (lower)
    {
    case 'p':
        type = PAWN;
        break;
    case 'r':
        type = ROOK;
        break;
    case 'n':
        type = KNIGHT;
        break;
    case 'b':
        type = BISHOP;
        break;
    case 'q':
        type = QUEEN;
        break;
    case 'k':
        type = KING;
        break;
    default:
        return p;
    }

    return (Piece){type, color};
}

// Convert algebraic square (e.g., "e3") to row/col
static Position algebraicToPos(const char *s)
{
    Position p = {-1, -1};
    if (strlen(s) != 2)
        return p;
    char file = s[0], rank = s[1];

    if (file < 'a' || file > 'h')
        return p;
    if (rank < '1' || rank > '8')
        return p;

    p.col = file - 'a';
    p.row = 8 - (rank - '0');
    return p;
}

// Convert row/col to algebraic (e3)
static void posToAlgebraic(Position pos, char out[3])
{
    if (pos.row == -1)
    {
        strcpy(out, "-");
        return;
    }
    out[0] = 'a' + pos.col;
    out[1] = '8' - pos.row;
    out[2] = '\0';
}

// -------------------- Load Board --------------------

bool loadBoardFromFile(const char *filename, BoardState *board)
{
    FILE *f = fopen(filename, "r");
    if (!f)
        return false;

    char line[64];

    // --- Read 8 board rows ---
    for (int r = 0; r < 8; r++)
    {
        if (!fgets(line, sizeof(line), f))
        {
            fclose(f);
            return false;
        }
        if ((int)strlen(line) < 8)
        {
            fclose(f);
            return false;
        }

        for (int c = 0; c < 8; c++)
        {
            board->squares[r][c] = charToPiece(line[c]);
        }
    }

    // --- Current player (w/b) ---
    if (!fgets(line, sizeof(line), f))
    {
        fclose(f);
        return false;
    }
    board->currentPlayer = (line[0] == 'w') ? WHITE : BLACK;

    // --- Castling rights ---
    if (!fgets(line, sizeof(line), f))
    {
        fclose(f);
        return false;
    }
    board->castling = (CastlingRights){0, 0, 0, 0};
    for (int i = 0; line[i] && line[i] != '\n'; i++)
    {
        switch (line[i])
        {
        case 'K':
            board->castling.wk = 1;
            break;
        case 'Q':
            board->castling.wq = 1;
            break;
        case 'k':
            board->castling.bk = 1;
            break;
        case 'q':
            board->castling.bq = 1;
            break;
        }
    }

    // --- En passant target ---
    if (!fgets(line, sizeof(line), f))
    {
        fclose(f);
        return false;
    }
    if (line[0] == '-')
    {
        board->enPassantTarget = (Position){-1, -1};
    }
    else
    {
        board->enPassantTarget = algebraicToPos(line);
    }

    // --- Halfmove clock ---
    if (!fgets(line, sizeof(line), f))
    {
        fclose(f);
        return false;
    }
    board->halfmoveClock = atoi(line);

    // --- Fullmove number ---
    if (!fgets(line, sizeof(line), f))
    {
        fclose(f);
        return false;
    }
    board->fullmoveNumber = atoi(line);

    fclose(f);
    return true;
}

// -------------------- Save Board --------------------

bool saveBoardToFile(const char *filename, const BoardState *board)
{
    FILE *f = fopen(filename, "w");
    if (!f)
        return false;

    // Write 8 rows
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            fputc(pieceToChar(board->squares[r][c]), f);
        }
        fputc('\n', f);
    }

    // Current player
    fprintf(f, "%c\n", (board->currentPlayer == WHITE ? 'w' : 'b'));

    // Castling rights
    char cast[5] = "";
    int idx = 0;
    if (board->castling.wk)
        cast[idx++] = 'K';
    if (board->castling.wq)
        cast[idx++] = 'Q';
    if (board->castling.bk)
        cast[idx++] = 'k';
    if (board->castling.bq)
        cast[idx++] = 'q';
    if (idx == 0)
        cast[idx++] = '-';
    cast[idx] = '\0';
    fprintf(f, "%s\n", cast);

    // En passant
    char ep[3];
    posToAlgebraic(board->enPassantTarget, ep);
    fprintf(f, "%s\n", ep);

    // Clocks
    fprintf(f, "%d\n", board->halfmoveClock);
    fprintf(f, "%d\n", board->fullmoveNumber);

    fclose(f);
    return true;
}
