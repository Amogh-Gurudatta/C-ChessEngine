#include "notation.h"
#include "game.h"
#include "ai.h"
#include "fileio.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ---------------- Shared helpers ---------------- */

static char sanPieceLetter(PieceType t)
{
    switch (t)
    {
    case KNIGHT:
        return 'N';
    case BISHOP:
        return 'B';
    case ROOK:
        return 'R';
    case QUEEN:
        return 'Q';
    case KING:
        return 'K';
    default:
        return '\0';
    }
}

static PieceType sanCharToPieceType(char c)
{
    switch (c)
    {
    case 'N':
        return KNIGHT;
    case 'B':
        return BISHOP;
    case 'R':
        return ROOK;
    case 'Q':
        return QUEEN;
    case 'K':
        return KING;
    default:
        return EMPTY;
    }
}

/* ---------------- Long algebraic ---------------- */

bool parseLongAlgebraic(const char *s, Move *out)
{
    Move m;
    m.flag = MOVE_NORMAL;
    m.promotion = EMPTY;

    size_t len = strlen(s);
    if (len < 4)
    {
        m.from = (Position){-1, -1};
        m.to = (Position){-1, -1};
        *out = m;
        return false;
    }

    m.from.col = s[0] - 'a';
    m.from.row = 8 - (s[1] - '0');
    m.to.col = s[2] - 'a';
    m.to.row = 8 - (s[3] - '0');

    if (len >= 5)
    {
        char p = (char)tolower((unsigned char)s[4]);
        switch (p)
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
        default:
            m.promotion = QUEEN;
            break;
        }
        m.flag = MOVE_PROMOTION;
    }

    *out = m;
    return true;
}

bool resolveMove(BoardState *board, Move raw, Move *out)
{
    MoveList list = generateAllLegalMoves(board);

    for (int i = 0; i < list.count; i++)
    {
        Move m = list.moves[i];

        if (m.from.row == raw.from.row && m.from.col == raw.from.col &&
            m.to.row == raw.to.row && m.to.col == raw.to.col)
        {
            if (m.flag == MOVE_PROMOTION)
            {
                if (raw.promotion == EMPTY)
                {
                    if (m.promotion == QUEEN)
                    {
                        *out = m;
                        return true;
                    }
                }
                else if (raw.promotion == m.promotion)
                {
                    *out = m;
                    return true;
                }
            }
            else
            {
                *out = m;
                return true;
            }
        }
    }
    return false;
}

void moveToLongAlgebraic(Move m, char *buf, size_t bufSize)
{
    char promoChar = '\0';
    if (m.flag == MOVE_PROMOTION)
    {
        promoChar = (char)tolower((unsigned char)sanPieceLetter(m.promotion));
        if (m.promotion == EMPTY)
            promoChar = '\0';
    }

    if (promoChar != '\0')
    {
        snprintf(buf, bufSize, "%c%d%c%d%c",
                 (char)('a' + m.from.col), 8 - m.from.row,
                 (char)('a' + m.to.col), 8 - m.to.row, promoChar);
    }
    else
    {
        snprintf(buf, bufSize, "%c%d%c%d",
                 (char)('a' + m.from.col), 8 - m.from.row,
                 (char)('a' + m.to.col), 8 - m.to.row);
    }
}

bool isLongAlgebraicFormat(const char *s)
{
    size_t len = strlen(s);
    if (len != 4 && len != 5)
        return false;
    if (s[0] < 'a' || s[0] > 'h')
        return false;
    if (s[1] < '1' || s[1] > '8')
        return false;
    if (s[2] < 'a' || s[2] > 'h')
        return false;
    if (s[3] < '1' || s[3] > '8')
        return false;
    if (len == 5)
    {
        char p = (char)tolower((unsigned char)s[4]);
        if (p != 'q' && p != 'r' && p != 'b' && p != 'n')
            return false;
    }
    return true;
}

/* ---------------- SAN ---------------- */

bool sanToMove(BoardState *board, const char *san, Move *out)
{
    char buf[24];
    size_t len = strlen(san);
    if (len == 0 || len >= sizeof(buf))
        return false;
    strcpy(buf, san);

    /* Strip trailing check/checkmate markers */
    while (len > 0 && (buf[len - 1] == '+' || buf[len - 1] == '#'))
    {
        buf[--len] = '\0';
    }
    if (len == 0)
        return false;

    /* Normalize "0-0"/"0-0-0" castling to "O-O"/"O-O-O" for comparison */
    char norm[24];
    for (size_t i = 0; i <= len; i++)
    {
        norm[i] = (buf[i] == '0') ? 'O' : buf[i];
    }

    MoveList list = generateAllLegalMoves(board);

    if (strcmp(norm, "O-O-O") == 0)
    {
        for (int i = 0; i < list.count; i++)
        {
            if (list.moves[i].flag == MOVE_CASTLE_QUEEN)
            {
                *out = list.moves[i];
                return true;
            }
        }
        return false;
    }
    if (strcmp(norm, "O-O") == 0)
    {
        for (int i = 0; i < list.count; i++)
        {
            if (list.moves[i].flag == MOVE_CASTLE_KING)
            {
                *out = list.moves[i];
                return true;
            }
        }
        return false;
    }

    /* Promotion suffix, e.g. "=Q" */
    PieceType promotion = EMPTY;
    char *eq = strchr(buf, '=');
    if (eq != NULL)
    {
        char p = (char)toupper((unsigned char)eq[1]);
        promotion = sanCharToPieceType(p);
        if (promotion == EMPTY)
            return false;
        *eq = '\0';
        len = strlen(buf);
    }

    size_t idx = 0;
    PieceType pieceType = PAWN;
    if (buf[0] >= 'A' && buf[0] <= 'Z')
    {
        pieceType = sanCharToPieceType(buf[0]);
        if (pieceType == EMPTY)
            return false;
        idx = 1;
    }

    /* Collect remaining chars, dropping the capture 'x' marker */
    char coords[8];
    size_t coordLen = 0;
    for (size_t i = idx; i < len; i++)
    {
        if (buf[i] == 'x' || buf[i] == 'X')
            continue;
        if (coordLen >= sizeof(coords) - 1)
            return false;
        coords[coordLen++] = buf[i];
    }
    coords[coordLen] = '\0';

    if (coordLen < 2 || coordLen > 4)
        return false;

    char destFileC = coords[coordLen - 2];
    char destRankC = coords[coordLen - 1];
    if (destFileC < 'a' || destFileC > 'h' || destRankC < '1' || destRankC > '8')
        return false;

    int destCol = destFileC - 'a';
    int destRow = 8 - (destRankC - '0');

    int disambigFile = -1, disambigRank = -1;
    if (coordLen == 3)
    {
        char d = coords[0];
        if (d >= 'a' && d <= 'h')
            disambigFile = d - 'a';
        else if (d >= '1' && d <= '8')
            disambigRank = 8 - (d - '0');
        else
            return false;
    }
    else if (coordLen == 4)
    {
        char df = coords[0], dr = coords[1];
        if (df < 'a' || df > 'h' || dr < '1' || dr > '8')
            return false;
        disambigFile = df - 'a';
        disambigRank = 8 - (dr - '0');
    }

    Move found = {0};
    int matchCount = 0;
    for (int i = 0; i < list.count; i++)
    {
        Move m = list.moves[i];
        Piece pieceAtFrom = board->squares[m.from.row][m.from.col];
        if (pieceAtFrom.type != pieceType)
            continue;
        if (m.to.row != destRow || m.to.col != destCol)
            continue;
        if (disambigFile != -1 && m.from.col != disambigFile)
            continue;
        if (disambigRank != -1 && m.from.row != disambigRank)
            continue;
        if (m.flag == MOVE_PROMOTION)
        {
            PieceType wantPromo = (promotion != EMPTY) ? promotion : QUEEN;
            if (m.promotion != wantPromo)
                continue;
        }
        else if (promotion != EMPTY)
        {
            continue;
        }
        found = m;
        matchCount++;
    }

    if (matchCount == 1)
    {
        *out = found;
        return true;
    }
    return false;
}

void moveToSan(BoardState *board, Move m, char *buf, size_t bufSize)
{
    Piece moving = board->squares[m.from.row][m.from.col];
    bool isCapture = (board->squares[m.to.row][m.to.col].type != EMPTY) || (m.flag == MOVE_EN_PASSANT);

    char core[24];
    size_t pos = 0;

    if (m.flag == MOVE_CASTLE_KING)
    {
        strcpy(core, "O-O");
        pos = strlen(core);
    }
    else if (m.flag == MOVE_CASTLE_QUEEN)
    {
        strcpy(core, "O-O-O");
        pos = strlen(core);
    }
    else
    {
        char pieceLetter = sanPieceLetter(moving.type);
        char disambig[3] = "";

        if (moving.type != PAWN)
        {
            MoveList list = generateAllLegalMoves(board);
            bool sameFile = false, sameRank = false, anyOther = false;
            for (int i = 0; i < list.count; i++)
            {
                Move o = list.moves[i];
                if (o.from.row == m.from.row && o.from.col == m.from.col)
                    continue;
                if (o.to.row != m.to.row || o.to.col != m.to.col)
                    continue;
                Piece otherPiece = board->squares[o.from.row][o.from.col];
                if (otherPiece.type != moving.type)
                    continue;
                anyOther = true;
                if (o.from.col == m.from.col)
                    sameFile = true;
                if (o.from.row == m.from.row)
                    sameRank = true;
            }
            if (anyOther)
            {
                if (!sameFile)
                {
                    disambig[0] = (char)('a' + m.from.col);
                    disambig[1] = '\0';
                }
                else if (!sameRank)
                {
                    disambig[0] = (char)('8' - m.from.row);
                    disambig[1] = '\0';
                }
                else
                {
                    disambig[0] = (char)('a' + m.from.col);
                    disambig[1] = (char)('8' - m.from.row);
                    disambig[2] = '\0';
                }
            }
        }

        if (pieceLetter != '\0')
        {
            core[pos++] = pieceLetter;
        }

        if (moving.type == PAWN && isCapture)
        {
            core[pos++] = (char)('a' + m.from.col);
        }
        else
        {
            size_t dLen = strlen(disambig);
            memcpy(core + pos, disambig, dLen);
            pos += dLen;
        }

        if (isCapture)
        {
            core[pos++] = 'x';
        }

        core[pos++] = (char)('a' + m.to.col);
        core[pos++] = (char)('8' - m.to.row);

        if (m.flag == MOVE_PROMOTION)
        {
            core[pos++] = '=';
            core[pos++] = sanPieceLetter(m.promotion);
        }

        core[pos] = '\0';
    }

    makeMove(board, m);
    PieceColor opponent = board->currentPlayer;
    bool inCheck = isKingInCheck(board, opponent);
    char suffix = '\0';
    if (inCheck)
    {
        MoveList replies = generateAllLegalMoves(board);
        suffix = (replies.count == 0) ? '#' : '+';
    }
    undoMove(board, m);

    if (suffix != '\0')
    {
        snprintf(buf, bufSize, "%s%c", core, suffix);
    }
    else
    {
        snprintf(buf, bufSize, "%s", core);
    }
}

bool parseUserMove(BoardState *board, const char *input, Move *out)
{
    if (isLongAlgebraicFormat(input))
    {
        Move raw;
        if (parseLongAlgebraic(input, &raw) && raw.from.row != -1)
        {
            if (resolveMove(board, raw, out))
                return true;
        }
        return false;
    }
    return sanToMove(board, input, out);
}

/* ---------------- FEN ---------------- */

bool boardToFen(const BoardState *board, char *buf, size_t bufSize)
{
    char placement[80];
    size_t pos = 0;

    for (int r = 0; r < 8; r++)
    {
        int emptyRun = 0;
        for (int c = 0; c < 8; c++)
        {
            Piece p = board->squares[r][c];
            if (p.type == EMPTY)
            {
                emptyRun++;
            }
            else
            {
                if (emptyRun > 0)
                {
                    placement[pos++] = (char)('0' + emptyRun);
                    emptyRun = 0;
                }
                placement[pos++] = pieceToChar(p);
            }
        }
        if (emptyRun > 0)
        {
            placement[pos++] = (char)('0' + emptyRun);
        }
        if (r < 7)
        {
            placement[pos++] = '/';
        }
    }
    placement[pos] = '\0';

    char castlingStr[5];
    int idx = 0;
    if (board->castling.wk)
        castlingStr[idx++] = 'K';
    if (board->castling.wq)
        castlingStr[idx++] = 'Q';
    if (board->castling.bk)
        castlingStr[idx++] = 'k';
    if (board->castling.bq)
        castlingStr[idx++] = 'q';
    if (idx == 0)
        castlingStr[idx++] = '-';
    castlingStr[idx] = '\0';

    char epStr[3];
    if (board->enPassantTarget.row == -1)
    {
        strcpy(epStr, "-");
    }
    else
    {
        epStr[0] = (char)('a' + board->enPassantTarget.col);
        epStr[1] = (char)('8' - board->enPassantTarget.row);
        epStr[2] = '\0';
    }

    int written = snprintf(buf, bufSize, "%s %c %s %s %d %d",
                            placement,
                            board->currentPlayer == WHITE ? 'w' : 'b',
                            castlingStr, epStr,
                            board->halfmoveClock, board->fullmoveNumber);

    return written > 0 && (size_t)written < bufSize;
}

bool fenToBoard(const char *fen, BoardState *board)
{
    char placement[80], activeColor[8], castlingStr[8], epStr[8];
    int halfmove = 0, fullmove = 1;

    int matched = sscanf(fen, "%79s %7s %7s %7s %d %d",
                          placement, activeColor, castlingStr, epStr, &halfmove, &fullmove);
    if (matched < 4)
        return false;

    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            board->squares[r][c] = (Piece){EMPTY, NO_COLOR};

    int r = 0, c = 0;
    for (size_t i = 0; placement[i] != '\0'; i++)
    {
        char ch = placement[i];
        if (ch == '/')
        {
            r++;
            c = 0;
            continue;
        }
        if (r >= 8)
            return false;
        if (isdigit((unsigned char)ch))
        {
            int n = ch - '0';
            for (int k = 0; k < n; k++)
            {
                if (c >= 8)
                    return false;
                board->squares[r][c++] = (Piece){EMPTY, NO_COLOR};
            }
        }
        else
        {
            if (c >= 8)
                return false;
            board->squares[r][c++] = charToPiece(ch);
        }
    }

    board->currentPlayer = (activeColor[0] == 'w') ? WHITE : BLACK;

    board->castling = (CastlingRights){0, 0, 0, 0};
    for (size_t i = 0; castlingStr[i] != '\0'; i++)
    {
        switch (castlingStr[i])
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
        default:
            break;
        }
    }

    if (epStr[0] == '-')
    {
        board->enPassantTarget = (Position){-1, -1};
    }
    else
    {
        board->enPassantTarget.col = epStr[0] - 'a';
        board->enPassantTarget.row = 8 - (epStr[1] - '0');
    }

    board->halfmoveClock = (matched >= 5) ? halfmove : 0;
    board->fullmoveNumber = (matched >= 6) ? fullmove : 1;

    return true;
}

/* ---------------- PGN ---------------- */

bool exportPgn(const char *filename, char sanLog[][SAN_MAX_LEN], int moveCount,
               const char *whiteName, const char *blackName, const char *result)
{
    FILE *f = fopen(filename, "w");
    if (!f)
        return false;

    time_t t = time(NULL);
    struct tm *tmInfo = localtime(&t);
    char dateStr[16];
    if (tmInfo != NULL)
        strftime(dateStr, sizeof(dateStr), "%Y.%m.%d", tmInfo);
    else
        strcpy(dateStr, "????.??.??");

    fprintf(f, "[Event \"Casual Game\"]\n");
    fprintf(f, "[Site \"Local\"]\n");
    fprintf(f, "[Date \"%s\"]\n", dateStr);
    fprintf(f, "[Round \"-\"]\n");
    fprintf(f, "[White \"%s\"]\n", whiteName);
    fprintf(f, "[Black \"%s\"]\n", blackName);
    fprintf(f, "[Result \"%s\"]\n\n", result);

    int lineLen = 0;
    for (int i = 0; i < moveCount; i++)
    {
        char moveNumBuf[16] = "";
        if (i % 2 == 0)
        {
            snprintf(moveNumBuf, sizeof(moveNumBuf), "%d. ", i / 2 + 1);
        }
        int chunkLen = (int)strlen(moveNumBuf) + (int)strlen(sanLog[i]) + 1;
        if (lineLen + chunkLen > 80)
        {
            fprintf(f, "\n");
            lineLen = 0;
        }
        fprintf(f, "%s%s ", moveNumBuf, sanLog[i]);
        lineLen += chunkLen;
    }
    fprintf(f, "%s\n", result);

    fclose(f);
    return true;
}
