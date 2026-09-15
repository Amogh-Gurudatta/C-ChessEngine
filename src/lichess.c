#include "lichess.h"
#include "structs.h"
#include "game.h"
#include "notation.h"
#include "fileio.h"
#include "ai.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LICHESS_API_BASE "https://lichess.org"
#define HTTP_BUF_SIZE 32768
#define STREAM_BUF_SIZE 65536

/* Defined in main.c; no dedicated UI header exists yet in this project. */
void printBoard(BoardState *board);

/* ---------------- Tiny flat JSON field extraction ----------------
 * Lichess's NDJSON lines are shallow enough that a substring search for
 * "key":"value" works without a real JSON parser. jsonFindNestedString
 * scopes the search to start at an outer object so "white"/"black" ids
 * (both named "id") don't collide. */

static bool jsonFindString(const char *json, const char *key, char *out, size_t outSize)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL)
        return false;
    p += strlen(pattern);

    size_t i = 0;
    while (*p != '\0' && *p != '"' && i + 1 < outSize)
    {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static bool jsonFindNestedString(const char *json, const char *outerKey, const char *innerKey,
                                  char *out, size_t outSize)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":{", outerKey);
    const char *p = strstr(json, pattern);
    if (p == NULL)
        return false;
    return jsonFindString(p, innerKey, out, outSize);
}

/* Numeric fields (wtime/btime/winc/binc are plain milliseconds, e.g.
 * "wtime":180000) aren't quoted, so they need their own extractor. */
static bool jsonFindNumber(const char *json, const char *key, double *out)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (p == NULL)
        return false;
    p += strlen(pattern);

    char *end = NULL;
    double value = strtod(p, &end);
    if (end == p)
        return false;

    *out = value;
    return true;
}

/* ---------------- HTTP plumbing ---------------- */

typedef struct
{
    char data[HTTP_BUF_SIZE];
    size_t len;
} HttpBuffer;

static size_t httpWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    HttpBuffer *buf = (HttpBuffer *)userdata;
    size_t total = size * nmemb;
    size_t room = (sizeof(buf->data) - 1) - buf->len;
    size_t toCopy = (total < room) ? total : room;

    memcpy(buf->data + buf->len, ptr, toCopy);
    buf->len += toCopy;
    buf->data[buf->len] = '\0';

    return total; /* report the full amount consumed even if we truncated */
}

static bool httpAuthedRequest(const char *url, const char *token, const char *method, HttpBuffer *out)
{
    CURL *curl = curl_easy_init();
    if (curl == NULL)
        return false;

    out->len = 0;
    out->data[0] = '\0';

    char authHeader[256];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", token);
    struct curl_slist *headers = curl_slist_append(NULL, authHeader);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, httpWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    if (strcmp(method, "POST") == 0)
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    }

    CURLcode res = curl_easy_perform(curl);

    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) && (httpStatus >= 200 && httpStatus < 300);
}

/* ---------------- Streaming (NDJSON) ---------------- */

typedef struct
{
    char buffer[STREAM_BUF_SIZE];
    size_t len;
    int alreadyApplied;
    char foundMove[16];
    bool foundNewMove;
    char status[32];
    bool statusChanged;
    /* Real Lichess clock, in milliseconds, from the most recent gameState
     * message seen (present on both the initial gameFull.state and every
     * subsequent stream update). */
    double wtimeMs, btimeMs, wincMs, bincMs;
    bool hasClockInfo;
} StreamState;

static void processStreamLine(const char *line, StreamState *state)
{
    if (line[0] == '\0')
        return;

    double wtime, btime;
    if (jsonFindNumber(line, "wtime", &wtime) && jsonFindNumber(line, "btime", &btime))
    {
        state->wtimeMs = wtime;
        state->btimeMs = btime;
        state->hasClockInfo = true;

        double winc, binc;
        if (jsonFindNumber(line, "winc", &winc))
            state->wincMs = winc;
        if (jsonFindNumber(line, "binc", &binc))
            state->bincMs = binc;
    }

    char movesStr[4096];
    if (jsonFindString(line, "moves", movesStr, sizeof(movesStr)))
    {
        char tmp[4096];
        strncpy(tmp, movesStr, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        int count = 0;
        char lastMove[16] = "";
        char *tok = strtok(tmp, " ");
        while (tok != NULL)
        {
            count++;
            strncpy(lastMove, tok, sizeof(lastMove) - 1);
            lastMove[sizeof(lastMove) - 1] = '\0';
            tok = strtok(NULL, " ");
        }

        if (count > state->alreadyApplied && lastMove[0] != '\0')
        {
            strncpy(state->foundMove, lastMove, sizeof(state->foundMove) - 1);
            state->foundMove[sizeof(state->foundMove) - 1] = '\0';
            state->foundNewMove = true;
        }
    }

    char statusStr[32];
    if (jsonFindString(line, "status", statusStr, sizeof(statusStr)))
    {
        if (strcmp(statusStr, state->status) != 0)
        {
            strncpy(state->status, statusStr, sizeof(state->status) - 1);
            state->status[sizeof(state->status) - 1] = '\0';
            state->statusChanged = true;
        }
    }
}

static size_t streamWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    StreamState *state = (StreamState *)userdata;
    size_t total = size * nmemb;

    size_t room = (sizeof(state->buffer) - 1) - state->len;
    size_t toCopy = (total < room) ? total : room;
    memcpy(state->buffer + state->len, ptr, toCopy);
    state->len += toCopy;
    state->buffer[state->len] = '\0';

    char *lineStart = state->buffer;
    char *newline;
    while ((newline = strchr(lineStart, '\n')) != NULL)
    {
        *newline = '\0';
        processStreamLine(lineStart, state);
        lineStart = newline + 1;
    }

    size_t remaining = strlen(lineStart);
    memmove(state->buffer, lineStart, remaining + 1);
    state->len = remaining;

    /* Abort the transfer once we have what we came for; the caller treats
     * the resulting CURLE_WRITE_ERROR as success, not a real failure. */
    if (state->foundNewMove || state->statusChanged)
        return 0;

    return total;
}

/* One blocking read against the game's event stream: returns once a new
 * opponent move appears, or the game status changes (e.g. ended). */
static bool lichessStreamOnce(const char *token, const char *gameId, int alreadyApplied,
                               StreamState *outState)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/board/game/stream/%s", LICHESS_API_BASE, gameId);

    memset(outState, 0, sizeof(*outState));
    outState->alreadyApplied = alreadyApplied;

    CURL *curl = curl_easy_init();
    if (curl == NULL)
        return false;

    char authHeader[256];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", token);
    struct curl_slist *headers = curl_slist_append(NULL, authHeader);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, outState);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    /* CURLE_WRITE_ERROR here just means our callback intentionally
     * aborted the transfer after finding what it needed. */
    return (res == CURLE_OK) || (res == CURLE_WRITE_ERROR);
}

/* ---------------- Public API ---------------- */

bool lichessGetToken(char *buf, size_t bufSize)
{
    const char *env = getenv("LICHESS_API_TOKEN");
    if (env == NULL || env[0] == '\0')
        return false;
    strncpy(buf, env, bufSize - 1);
    buf[bufSize - 1] = '\0';
    return true;
}

void playLichessGame(const char *gameId, bool botMode)
{
    char token[128];
    if (!lichessGetToken(token, sizeof(token)))
    {
        printf("LICHESS_API_TOKEN is not set. Create a personal API token at\n");
        printf("https://lichess.org/account/oauth/token and export it, e.g.:\n");
        printf("  export LICHESS_API_TOKEN=lip_xxxxxxxxxxxx\n");
        return;
    }

    HttpBuffer accountInfo;
    char accountUrl[128];
    snprintf(accountUrl, sizeof(accountUrl), "%s/api/account", LICHESS_API_BASE);
    if (!httpAuthedRequest(accountUrl, token, "GET", &accountInfo))
    {
        printf("Could not reach Lichess (check your token and network connection).\n");
        return;
    }

    char myId[64];
    if (!jsonFindString(accountInfo.data, "id", myId, sizeof(myId)))
    {
        printf("Could not determine Lichess account id from response.\n");
        return;
    }
    printf("Logged in to Lichess as: %s\n", myId);

    /* First read of the game stream gives us the gameFull payload: our
     * color, both player ids, the starting FEN (if any) and moves so far. */
    StreamState initial;
    if (!lichessStreamOnce(token, gameId, 0, &initial))
    {
        printf("Could not open game stream for game '%s'.\n", gameId);
        return;
    }

    char whiteId[64] = "", blackId[64] = "";
    jsonFindNestedString(initial.buffer, "white", "id", whiteId, sizeof(whiteId));
    jsonFindNestedString(initial.buffer, "black", "id", blackId, sizeof(blackId));

    PieceColor myColor = WHITE;
    for (size_t i = 0; whiteId[i]; i++)
        whiteId[i] = (char)tolower((unsigned char)whiteId[i]);
    for (size_t i = 0; blackId[i]; i++)
        blackId[i] = (char)tolower((unsigned char)blackId[i]);
    char myIdLower[64];
    strncpy(myIdLower, myId, sizeof(myIdLower) - 1);
    myIdLower[sizeof(myIdLower) - 1] = '\0';
    for (size_t i = 0; myIdLower[i]; i++)
        myIdLower[i] = (char)tolower((unsigned char)myIdLower[i]);

    if (strcmp(blackId, myIdLower) == 0)
        myColor = BLACK;

    printf("Playing as %s in game %s.\n", myColor == WHITE ? "White" : "Black", gameId);

    BoardState board;
    char initialFen[FEN_MAX_LEN];
    if (jsonFindString(initial.buffer, "initialFen", initialFen, sizeof(initialFen)) &&
        strcmp(initialFen, "startpos") != 0)
    {
        if (!fenToBoard(initialFen, &board))
        {
            printf("Could not parse starting FEN from Lichess; aborting.\n");
            return;
        }
    }
    else
    {
        const char *start[8] = {
            "rnbqkbnr", "pppppppp", "........", "........",
            "........", "........", "PPPPPPPP", "RNBQKBNR"};
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                board.squares[r][c] = charToPiece(start[r][c]);
        board.currentPlayer = WHITE;
        board.castling = (CastlingRights){1, 1, 1, 1};
        board.enPassantTarget = (Position){-1, -1};
        board.halfmoveClock = 0;
        board.fullmoveNumber = 1;
    }

    /* Replay any moves already played (e.g. reconnecting mid-game). */
    int appliedCount = 0;
    char movesSoFar[4096];
    if (jsonFindString(initial.buffer, "moves", movesSoFar, sizeof(movesSoFar)))
    {
        char *tok = strtok(movesSoFar, " ");
        while (tok != NULL)
        {
            Move raw, resolved;
            if (parseLongAlgebraic(tok, &raw) && resolveMove(&board, raw, &resolved))
            {
                makeMove(&board, resolved);
                appliedCount++;
            }
            tok = strtok(NULL, " ");
        }
    }

    char status[32] = "started";
    if (initial.status[0] != '\0')
    {
        snprintf(status, sizeof(status), "%s", initial.status);
    }

    // Real Lichess clock, if the game has one; botMode uses this (via
    // findBestMoveTimed) instead of guessing at a time budget.
    bool haveRealClock = initial.hasClockInfo;
    double whiteMs = initial.wtimeMs, blackMs = initial.btimeMs;
    double whiteIncMs = initial.wincMs, blackIncMs = initial.bincMs;

    if (botMode)
        printf("Bot mode: the engine will play %s's moves automatically.\n",
               myColor == WHITE ? "White" : "Black");

    while (strcmp(status, "started") == 0)
    {
        printBoard(&board);

        if (board.currentPlayer == myColor)
        {
            Move finalMove;

            if (botMode)
            {
                double myRemainingSeconds = ((myColor == WHITE) ? whiteMs : blackMs) / 1000.0;
                double myIncSeconds = ((myColor == WHITE) ? whiteIncMs : blackIncMs) / 1000.0;

                finalMove = haveRealClock
                                ? findBestMoveTimed(&board, myRemainingSeconds, myIncSeconds)
                                : findBestMove(&board);
            }
            else
            {
                printf("\nYour move (e.g. e2e4, e4, Nf3): ");
                char input[32];
                if (scanf("%31s", input) != 1)
                    break;

                if (!parseUserMove(&board, input, &finalMove))
                {
                    printf("Illegal move. Please try again.\n");
                    continue;
                }
            }

            char uci[8];
            moveToLongAlgebraic(finalMove, uci, sizeof(uci));

            char moveUrl[256];
            snprintf(moveUrl, sizeof(moveUrl), "%s/api/board/game/%s/move/%s",
                     LICHESS_API_BASE, gameId, uci);
            HttpBuffer resp;
            if (!httpAuthedRequest(moveUrl, token, "POST", &resp))
            {
                printf("Failed to send move to Lichess.\n");
                continue;
            }

            if (botMode)
                printf("Bot plays: %s\n", uci);

            makeMove(&board, finalMove);
            appliedCount++;
        }
        else
        {
            printf("\nWaiting for opponent's move...\n");
            StreamState next;
            if (!lichessStreamOnce(token, gameId, appliedCount, &next))
            {
                printf("Lost connection to game stream.\n");
                break;
            }

            if (next.hasClockInfo)
            {
                haveRealClock = true;
                whiteMs = next.wtimeMs;
                blackMs = next.btimeMs;
                whiteIncMs = next.wincMs;
                blackIncMs = next.bincMs;
            }

            if (next.foundNewMove)
            {
                Move raw, resolved;
                if (parseLongAlgebraic(next.foundMove, &raw) && resolveMove(&board, raw, &resolved))
                {
                    makeMove(&board, resolved);
                    appliedCount++;
                }
            }
            if (next.statusChanged)
            {
                strncpy(status, next.status, sizeof(status) - 1);
                status[sizeof(status) - 1] = '\0';
            }
        }
    }

    printBoard(&board);
    printf("\nGame over. Status: %s\n", status);
}
