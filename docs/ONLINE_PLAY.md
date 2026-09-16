# Online Play & Chess Clocks

Covers `include/lichess.h`/`src/lichess.c` (optional, `make LICHESS=1` only) and `include/timecontrol.h`/`src/timecontrol.c`. See [README.md](README.md) for the doc index and [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md#time-management) for how the AI actually spends a time budget once it has one.

## Chess clocks (`timecontrol.c`)

A small, standalone module — a `ChessClock` is just `{whiteSeconds, blackSeconds, incrementSeconds}` plus four functions (`clockInit`, `clockConsume`, `clockHasFlagged`, `clockFormat`). It has no knowledge of the board, the search, or HTTP — it's shared, unmodified, between:

- **Local play** (`main.c`, `--clock <minutes>+<increment>`): `main.c` owns a `ChessClock`, timestamps the start of each side's turn with `time()` (wall-clock, not `clock()` — CPU time would read ~0 while blocked waiting for the human's stdin input), and calls `clockConsume()` once a move is decided.
- **Lichess play** (`lichess.c`, any game with a real time control): rather than tracking its own clock, `lichess.c` *mirrors* the authoritative `wtime`/`btime`/`winc`/`binc` fields Lichess reports in every game-state update, via `clockTimeFor()`'s direct pointer access.

### Flag-fall ordering

`clockConsume(clock, side, elapsed)` subtracts `elapsed` first, and only adds the increment back if that didn't deplete the clock (returns `false` on flag-fall, withholding the increment). `main.c` calls this **before** committing a move to the board — a move that was only decided after the flag had already fallen doesn't count, matching how a physical chess clock actually works (you lose the instant your flag falls, regardless of whether you'd already found a good move). Getting this ordering right matters: an earlier version of this checked the clock *after* applying and logging the move, which meant a losing-on-time player's move still showed up in the PGN.

## Lichess Board & Bot APIs (`lichess.c`)

Talks to Lichess's [Board API](https://lichess.org/api#tag/Board) and, for `--bot` mode, its separate [Bot API](https://lichess.org/api#tag/Bot) over HTTP (libcurl). Only compiled in with `make LICHESS=1`, keeping the rest of the project dependency-free by default. There's no chess.com support because chess.com's public API is read-only (stats/archives) — it has no live-play endpoint to talk to.

### Board API vs. Bot API — why `--bot` uses a different one entirely

These are two separate Lichess API surfaces with the same request/response shape (same `GameFullEvent`/`GameStateEvent` NDJSON schema, same `.../move/{move}` semantics) but different rules, and the engine picks between them based on `--bot`:

| | Board API (`/api/board/...`) | Bot API (`/api/bot/...`) |
| --- | --- | --- |
| Used when | plain `--lichess <gameId>` (you type the moves) | `--lichess <gameId> --bot` |
| Token scope | `board:play` | `bot:play` |
| Account | any normal Lichess account | a dedicated **Bot account** only |
| Engine assistance | **forbidden** ([fair play](https://lichess.org/page/fair-play)) | explicitly allowed - that's the point of it |
| Time controls | Rapid/Classical/Correspondence always; Blitz only for direct challenges, games vs AI, or bulk pairing; **Bullet and UltraBullet are never supported** | all of them except UltraBullet (¼+0) |

Lichess's own Board API docs are explicit that engine assistance is forbidden there — it's meant for physical boards and third-party clients relaying a *human's* moves. So `--bot` doesn't just add a flag to the Board API calls; it switches `apiSegment` (in `playLichessGame()`) to `"bot"` for every URL, which is the only way to legitimately have this engine play automatically.

**Getting a Bot account**: `POST /api/bot/account/upgrade` (with a `bot:play`-scoped token) upgrades a normal account to a Bot account - but **the account must never have played a single game first**, and the upgrade is **irreversible** (a Bot account can never play rated games again or revert to normal). This means it has to be a fresh, dedicated account made specifically for bot testing, never your main one. `playLichessGame()` checks this itself before doing anything else: it reads `/api/account`'s `title` field and refuses to proceed with `--bot` unless it's exactly `"BOT"`, printing the upgrade command rather than ever attempting it automatically - that's a one-way decision on your account, and this engine won't make it for you.

### Scope (v1)

The user creates or accepts the game on lichess.org first (or, for `--bot`, via the Bot account's own challenges) and passes its game ID on the command line (`--lichess <gameId>`); the engine doesn't do matchmaking (challenge creation, seeking) itself. This avoids needing the account-level event stream, only the per-game one.

### Lightweight JSON, on purpose

Rather than a real JSON library, `lichess.c` uses two tiny `strstr`-based extractors: `jsonFindString()` for quoted string fields (`"key":"value"`) and `jsonFindNumber()` for numeric ones (`"key":123`), plus `jsonFindNestedString()` to scope a search inside an outer object (needed because both `white` and `black` objects have an `id` field). This works because Lichess's NDJSON messages are shallow — a real parser would be correct in more edge cases but is unjustified complexity for this message shape. If a future feature needs deeper/more robust parsing, that's the point to reconsider.

`jsonFindNestedString()` bounds its search to the matching `{...}` braces of the outer object (copied into a small local buffer) rather than just starting a substring search from the object's opening brace and never stopping. This matters against an AI opponent: its player object has a literal JSON `null` for `"id"` (not a quoted string), so an unbounded search for `"id":"..."` inside it would fall through past the closing brace and match the *next* object's `id` instead — silently attributing the wrong account to that color.

### Determining your color

`playLichessGame()` reads both `white.id` and `black.id` from the game-start payload and compares each, lowercased, against your own account id from `/api/account`. Because an AI opponent's `id` is `null` rather than a string, "has an id" and "id matches me" are tracked as two separate booleans (`haveWhiteId`/`haveBlackId` vs `whiteIsMe`/`blackIsMe`): if neither side's id textually matches yours, but exactly one side has no id at all, that id-less side must be the one you're playing (the other one is the named opponent). Only if both ids are present and neither matches — which shouldn't happen for a game you're actually in — does it fall back to assuming White, with a printed warning.

### Streaming

`GET /api/board/game/stream/{id}` is a long-lived NDJSON stream (one JSON object per line): a `gameFull` object first, then a `gameState` object per event. `lichessStreamOnce()` does one blocking `curl_easy_perform()` against it, with a write callback (`streamWriteCallback`) that buffers partial lines and returns `0` (aborting the transfer) the moment it sees either a new move or a status change — the caller treats the resulting `CURLE_WRITE_ERROR` as (probably) success, since that's usually just the callback getting what it came for and cutting the connection. It's only *actually* treated as success once the HTTP status is also confirmed to be 2xx — a short error body (e.g. a 404 for a bad game ID) is small enough to reach the callback too, and without the status check would otherwise look identical to "found nothing interesting yet, keep going" and be silently accepted. This gives synchronous, single-threaded code (`playLichessGame()`'s loop just calls `lichessStreamOnce()` again each time it needs to wait for the opponent) at the cost of true always-on streaming, which is a reasonable trade for a console client with no background threads.

### Authentication & error diagnostics

A personal API token in `LICHESS_API_TOKEN`, sent as `Authorization: Bearer <token>` on every request - `board:play`-scoped for plain `--lichess`, `bot:play`-scoped for `--bot` (see the table above; the two scopes aren't interchangeable). **Reading** (account info, streaming a game) only needs a valid token of the right kind; **writing** (submitting a move) additionally needs that specific scope — a token missing it authenticates fine for the first two and is refused (HTTP 403) only when a move is actually sent, which looks exactly like "everything connects, but moves fail" from the outside.

Every HTTP call in this file threads an `outStatus` out-parameter back to its caller (`httpAuthedRequest()`, `lichessStreamOnce()`), and `printHttpFailure()` prints the HTTP status plus Lichess's response body (typically a short JSON error like `{"error":"Missing scope"}`) verbatim on any failure, with a one-line hint for 401 (bad/expired token) and 403 (missing scope). Surfacing the real error here matters: a generic "it didn't work" is nearly undiagnosable for an HTTP integration, where the failure reason is almost always sitting right there in the response. `playLichessGame()`'s own stream-open failure handler adds one more targeted hint: an HTTP 400 there on plain (non-bot) `--lichess` almost always means the game's time control isn't Board-API-eligible (see the table above), not a code bug.

### Bot mode (`--bot`)

Without `--bot`, `lichess.c` only relays moves you type (via the same `notation.c`'s `parseUserMove()` the local game loop uses) against the Board API — there's no AI involvement in a normal `--lichess` session. With `--bot`, every request goes to the Bot API instead (see the table above), and on the engine's turn it calls `ai.c`'s `findBestMoveTimed()` directly, using whichever `wtime`/`btime`/`winc`/`binc` the most recent stream message reported for its own color — the *actual* match clock, not a guess. If a game somehow has no time control info at all (`hasClockInfo` never set), bot mode falls back to the flat `findBestMove()`/`setSearchTimeLimit()` behavior described in [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md).
