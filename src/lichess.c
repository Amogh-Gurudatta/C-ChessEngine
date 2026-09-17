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
void printBoard(BoardState *board, PieceColor perspective);

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
    const char *objStart = strstr(json, pattern);
    if (objStart == NULL)
        return false;
    objStart += strlen(pattern) - 1; // back up to point at the '{' itself

    // Bound the search to this object only (matching braces), so a field
    // that's absent here (e.g. a null "id" on an AI opponent) can't leak
    // into a later sibling object that happens to have the same key.
    int depth = 0;
    const char *p = objStart;
    const char *objEnd = NULL;
    for (; *p != '\0'; p++)
    {
        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            depth--;
            if (depth == 0)
            {
                objEnd = p;
                break;
            }
        }
    }
    if (objEnd == NULL)
        return false;

    size_t objLen = (size_t)(objEnd - objStart) + 1;
    char scoped[512];
    if (objLen >= sizeof(scoped))
        objLen = sizeof(scoped) - 1;
    memcpy(scoped, objStart, objLen);
    scoped[objLen] = '\0';

    return jsonFindString(scoped, innerKey, out, outSize);
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

/* outStatus is always set (to 0 if curl itself failed before getting a
 * response) so a caller can print a genuinely diagnostic error - Lichess's
 * error responses are typically small JSON bodies like {"error":"Missing
 * scope"} that say exactly what went wrong, e.g. a token created without
 * the "board:play" scope will authenticate fine for reads (this call
 * succeeding) but be refused (typically HTTP 403) for anything that
 * plays/manages a game, like submitting a move. */
static bool httpAuthedRequest(const char *url, const char *token, const char *method, HttpBuffer *out, long *outStatus)
{
    CURL *curl = curl_easy_init();
    if (curl == NULL)
    {
        if (outStatus != NULL)
            *outStatus = 0;
        return false;
    }

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
    if (outStatus != NULL)
        *outStatus = httpStatus;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) && (httpStatus >= 200 && httpStatus < 300);
}

/* Prints an HTTP failure with whatever diagnostic detail is available -
 * status code and Lichess's (usually JSON) error body - instead of a bare
 * "it didn't work". */
static void printHttpFailure(const char *what, long status, const HttpBuffer *response)
{
    printf("%s (HTTP %ld): %s\n", what, status, (response->data[0] != '\0') ? response->data : "(no response body)");
    if (status == 401)
        printf("Your token may be invalid or expired - create a new one at https://lichess.org/account/oauth/token\n");
    else if (status == 403)
        printf("This usually means your token is missing the \"Play games with the board API\" (board:play)\n"
               "scope - create a new token at https://lichess.org/account/oauth/token with that box checked.\n");
}

/* ---------------- Streaming (NDJSON) ---------------- */

typedef struct
{
    char buffer[STREAM_BUF_SIZE];
    size_t len;
    /* A verbatim copy of the most recently completed NDJSON line, taken
     * before streamWriteCallback() trims a processed line out of `buffer`.
     * playLichessGame() reads fields (white/black id, initialFen, moves)
     * directly out of raw JSON text via jsonFindString()/
     * jsonFindNestedString() rather than pre-extracted struct fields for
     * those - it needs this snapshot, since by the time a call returns,
     * `buffer` itself typically only holds trailing not-yet-newline-
     * terminated bytes (often nothing at all). */
    char lastCompleteLine[STREAM_BUF_SIZE];
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
        strncpy(state->lastCompleteLine, lineStart, sizeof(state->lastCompleteLine) - 1);
        state->lastCompleteLine[sizeof(state->lastCompleteLine) - 1] = '\0';
        processStreamLine(lineStart, state);
        lineStart = newline + 1;
    }

    size_t remaining = strlen(lineStart);
    memmove(state->buffer, lineStart, remaining + 1);
    state->len = remaining;

    /* Never abort the transfer: this connection is meant to stay open for
     * the whole game (see PersistentStream below) - the caller notices
     * foundNewMove/statusChanged and simply stops polling for now, rather
     * than the old design of tearing the connection down and reopening it
     * fresh on every wait. Repeatedly reopening this exact endpoint is
     * what a real, sufficiently long game was observed to get rate-limited
     * for (HTTP 429) - see docs/ONLINE_PLAY.md. */
    return total;
}

/* Holds ONE long-lived connection to the game's event stream for the
 * entire game, opened once by lichessStreamOpen() and closed once by
 * lichessStreamClose() - see docs/ONLINE_PLAY.md for why this replaced an
 * earlier design that opened a fresh connection to this same endpoint on
 * every single wait, which was observed to get rate-limited (HTTP 429) on
 * a real, long-enough game. */
typedef struct
{
    CURL *easy;
    CURLM *multi;
    struct curl_slist *headers;
    StreamState state;
} PersistentStream;

/* Drives the already-open connection (curl_multi_perform()/curl_multi_poll(),
 * never a new HTTP request) until streamWriteCallback() flags a new move or
 * status change, or the transfer itself ends. Used both to read the very
 * first line (the gameFull payload, via lichessStreamOpen()) and every
 * subsequent wait (via lichessStreamWait()) - in both cases "something
 * happened" is exactly the same condition to poll for. */
static bool lichessStreamPump(PersistentStream *pstream, long *outStatus)
{
    int stillRunning = 1;
    while (stillRunning)
    {
        CURLMcode mc = curl_multi_perform(pstream->multi, &stillRunning);
        if (mc != CURLM_OK)
            break;

        if (pstream->state.foundNewMove || pstream->state.statusChanged)
            return true;

        if (stillRunning)
        {
            int numfds = 0;
            curl_multi_poll(pstream->multi, NULL, 0, 1000, &numfds);
        }
    }

    /* The transfer ended (or curl_multi_perform() itself failed) without
     * ever giving us a new event - read back the final HTTP status, if
     * any, so the caller can report a genuinely diagnostic message. */
    long httpStatus = 0;
    int msgsLeft;
    CURLMsg *msg;
    while ((msg = curl_multi_info_read(pstream->multi, &msgsLeft)) != NULL)
    {
        if (msg->msg == CURLMSG_DONE)
            curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &httpStatus);
    }
    if (outStatus != NULL)
        *outStatus = httpStatus;
    return false;
}

/* Opens the persistent connection and blocks until the first line (the
 * gameFull payload) has arrived. That line always flips state.status from
 * "" to a real value ("started", ...), so statusChanged is a reliable
 * signal that the bootstrap data has arrived - the same signal
 * lichessStreamWait() below waits on for every later event too.
 * apiSegment is "board" or "bot" - see playLichessGame()'s note on why bot
 * mode needs an entirely different set of endpoints, not just a flag. */
static bool lichessStreamOpen(PersistentStream *pstream, const char *apiSegment, const char *token,
                               const char *gameId, long *outStatus)
{
    memset(pstream, 0, sizeof(*pstream));

    char url[256];
    snprintf(url, sizeof(url), "%s/api/%s/game/stream/%s", LICHESS_API_BASE, apiSegment, gameId);

    pstream->easy = curl_easy_init();
    if (pstream->easy == NULL)
    {
        if (outStatus != NULL)
            *outStatus = 0;
        return false;
    }

    char authHeader[256];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", token);
    pstream->headers = curl_slist_append(NULL, authHeader);

    curl_easy_setopt(pstream->easy, CURLOPT_URL, url);
    curl_easy_setopt(pstream->easy, CURLOPT_HTTPHEADER, pstream->headers);
    curl_easy_setopt(pstream->easy, CURLOPT_WRITEFUNCTION, streamWriteCallback);
    curl_easy_setopt(pstream->easy, CURLOPT_WRITEDATA, &pstream->state);

    pstream->multi = curl_multi_init();
    if (pstream->multi == NULL)
    {
        // Left for the caller's lichessStreamClose() to tidy up (safe even
        // half-initialized like this - see its own NULL checks), rather
        // than duplicating cleanup here too.
        if (outStatus != NULL)
            *outStatus = 0;
        return false;
    }
    curl_multi_add_handle(pstream->multi, pstream->easy);

    return lichessStreamPump(pstream, outStatus);
}

/* Resumes reading from the already-open connection (no new HTTP request)
 * until a new opponent move appears or the game status changes. Returns
 * false if the underlying connection has ended/failed - the caller should
 * treat this the same as a lost connection (see playLichessGame()). */
static bool lichessStreamWait(PersistentStream *pstream, int alreadyApplied, long *outStatus)
{
    pstream->state.alreadyApplied = alreadyApplied;
    pstream->state.foundNewMove = false;
    pstream->state.statusChanged = false;
    return lichessStreamPump(pstream, outStatus);
}

static void lichessStreamClose(PersistentStream *pstream)
{
    if (pstream->multi != NULL && pstream->easy != NULL)
        curl_multi_remove_handle(pstream->multi, pstream->easy);
    if (pstream->easy != NULL)
        curl_easy_cleanup(pstream->easy);
    if (pstream->multi != NULL)
        curl_multi_cleanup(pstream->multi);
    if (pstream->headers != NULL)
        curl_slist_free_all(pstream->headers);
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
    /* The Board API (/api/board/...) is for humans relaying moves from a
     * physical board or third-party client, and explicitly forbids engine
     * assistance. Having this engine play automatically is only allowed
     * through the separate Bot API (/api/bot/...), which needs its own
     * "bot:play" scope and a dedicated Bot account (a one-way upgrade -
     * see the check below). See docs/ONLINE_PLAY.md for the full story. */
    const char *apiSegment = botMode ? "bot" : "board";

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
    long httpStatus = 0;
    snprintf(accountUrl, sizeof(accountUrl), "%s/api/account", LICHESS_API_BASE);
    if (!httpAuthedRequest(accountUrl, token, "GET", &accountInfo, &httpStatus))
    {
        printHttpFailure("Could not reach Lichess", httpStatus, &accountInfo);
        return;
    }

    char myId[64];
    if (!jsonFindString(accountInfo.data, "id", myId, sizeof(myId)))
    {
        printf("Could not determine Lichess account id from response.\n");
        return;
    }
    printf("Logged in to Lichess as: %s\n", myId);

    if (botMode)
    {
        char title[16] = "";
        jsonFindString(accountInfo.data, "title", title, sizeof(title));
        if (strcmp(title, "BOT") != 0)
        {
            printf("--bot needs a genuine Lichess Bot account, but '%s' isn't one.\n", myId);
            printf("Upgrading is IRREVERSIBLE, only works on an account that has NEVER played a\n");
            printf("game, and needs a token with the \"bot:play\" scope (not \"board:play\"):\n");
            printf("  curl -d '' https://lichess.org/api/bot/account/upgrade -H \"Authorization: Bearer <token>\"\n");
            printf("See docs/ONLINE_PLAY.md for details.\n");
            return;
        }
    }

    /* Opens ONE connection to the game stream for the entire game (see
     * PersistentStream above). Its first line gives us the gameFull
     * payload: our color, both player ids, the starting FEN (if any), and
     * moves so far. */
    PersistentStream pstream;
    if (!lichessStreamOpen(&pstream, apiSegment, token, gameId, &httpStatus))
    {
        printf("Could not open game stream for game '%s' (HTTP %ld).\n", gameId, httpStatus);
        if (httpStatus == 404)
            printf("Double-check the game ID - it's the part after lichess.org/ in the game's URL.\n");
        else if (httpStatus == 400 && !botMode)
            printf("The Board API only supports Rapid/Classical/Correspondence time controls (plus\n"
                   "Blitz for direct challenges) - Bullet and UltraBullet aren't supported at all.\n");
        else if (httpStatus == 429)
            printf("Rate limited by Lichess - wait a while before trying to reconnect.\n");
        lichessStreamClose(&pstream);
        return;
    }

    char whiteId[64] = "", blackId[64] = "";
    bool haveWhiteId = jsonFindNestedString(pstream.state.lastCompleteLine, "white", "id", whiteId, sizeof(whiteId));
    bool haveBlackId = jsonFindNestedString(pstream.state.lastCompleteLine, "black", "id", blackId, sizeof(blackId));

    for (size_t i = 0; whiteId[i]; i++)
        whiteId[i] = (char)tolower((unsigned char)whiteId[i]);
    for (size_t i = 0; blackId[i]; i++)
        blackId[i] = (char)tolower((unsigned char)blackId[i]);
    char myIdLower[64];
    strncpy(myIdLower, myId, sizeof(myIdLower) - 1);
    myIdLower[sizeof(myIdLower) - 1] = '\0';
    for (size_t i = 0; myIdLower[i]; i++)
        myIdLower[i] = (char)tolower((unsigned char)myIdLower[i]);

    // An AI opponent's id is JSON null (no quoted string), so "have an id"
    // and "id matches me" have to be checked separately - a side with no id
    // can never be a match, but it's still informative: if the other side
    // *does* have an id and it isn't ours, the id-less side must be us.
    bool whiteIsMe = haveWhiteId && strcmp(whiteId, myIdLower) == 0;
    bool blackIsMe = haveBlackId && strcmp(blackId, myIdLower) == 0;

    PieceColor myColor;
    if (whiteIsMe)
        myColor = WHITE;
    else if (blackIsMe)
        myColor = BLACK;
    else if (!haveWhiteId && haveBlackId)
        myColor = WHITE;
    else if (!haveBlackId && haveWhiteId)
        myColor = BLACK;
    else
    {
        printf("Warning: could not determine which color you're playing (assuming White) -\n");
        printf("if that's wrong, moves may be sent on the wrong turn.\n");
        myColor = WHITE;
    }

    printf("Playing as %s in game %s.\n", myColor == WHITE ? "White" : "Black", gameId);

    BoardState board;
    char initialFen[FEN_MAX_LEN];
    if (jsonFindString(pstream.state.lastCompleteLine, "initialFen", initialFen, sizeof(initialFen)) &&
        strcmp(initialFen, "startpos") != 0)
    {
        if (!fenToBoard(initialFen, &board))
        {
            printf("Could not parse starting FEN from Lichess; aborting.\n");
            lichessStreamClose(&pstream);
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
    if (jsonFindString(pstream.state.lastCompleteLine, "moves", movesSoFar, sizeof(movesSoFar)))
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
    if (pstream.state.status[0] != '\0')
    {
        snprintf(status, sizeof(status), "%s", pstream.state.status);
    }

    // Real Lichess clock, if the game has one; botMode uses this (via
    // findBestMoveTimed) instead of guessing at a time budget.
    bool haveRealClock = pstream.state.hasClockInfo;
    double whiteMs = pstream.state.wtimeMs, blackMs = pstream.state.btimeMs;
    double whiteIncMs = pstream.state.wincMs, blackIncMs = pstream.state.bincMs;

    if (botMode)
        printf("Bot mode: the engine will play %s's moves automatically.\n",
               myColor == WHITE ? "White" : "Black");

    while (strcmp(status, "started") == 0)
    {
        printBoard(&board, myColor);

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
            snprintf(moveUrl, sizeof(moveUrl), "%s/api/%s/game/%s/move/%s",
                     LICHESS_API_BASE, apiSegment, gameId, uci);
            HttpBuffer resp;
            if (!httpAuthedRequest(moveUrl, token, "POST", &resp, &httpStatus))
            {
                printHttpFailure("Failed to send move to Lichess", httpStatus, &resp);
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
            if (!lichessStreamWait(&pstream, appliedCount, &httpStatus))
            {
                printf("Lost connection to game stream (HTTP %ld).\n", httpStatus);
                if (httpStatus == 429)
                    printf("Rate limited by Lichess - if this keeps happening, wait a while before\n"
                           "reconnecting rather than retrying immediately.\n");
                break;
            }

            if (pstream.state.hasClockInfo)
            {
                haveRealClock = true;
                whiteMs = pstream.state.wtimeMs;
                blackMs = pstream.state.btimeMs;
                whiteIncMs = pstream.state.wincMs;
                blackIncMs = pstream.state.bincMs;
            }

            if (pstream.state.foundNewMove)
            {
                Move raw, resolved;
                if (parseLongAlgebraic(pstream.state.foundMove, &raw) && resolveMove(&board, raw, &resolved))
                {
                    makeMove(&board, resolved);
                    appliedCount++;
                }
            }
            if (pstream.state.statusChanged)
            {
                strncpy(status, pstream.state.status, sizeof(status) - 1);
                status[sizeof(status) - 1] = '\0';
            }
        }
    }

    lichessStreamClose(&pstream);
    printBoard(&board, myColor);
    printf("\nGame over. Status: %s\n", status);
}
