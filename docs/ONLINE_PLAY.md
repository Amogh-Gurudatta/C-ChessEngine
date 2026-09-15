# Online Play & Chess Clocks

Covers `include/lichess.h`/`src/lichess.c` (optional, `make LICHESS=1` only) and `include/timecontrol.h`/`src/timecontrol.c`. See [README.md](README.md) for the doc index and [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md#time-management) for how the AI actually spends a time budget once it has one.

## Chess clocks (`timecontrol.c`)

A small, standalone module — a `ChessClock` is just `{whiteSeconds, blackSeconds, incrementSeconds}` plus four functions (`clockInit`, `clockConsume`, `clockHasFlagged`, `clockFormat`). It has no knowledge of the board, the search, or HTTP — it's shared, unmodified, between:

- **Local play** (`main.c`, `--clock <minutes>+<increment>`): `main.c` owns a `ChessClock`, timestamps the start of each side's turn with `time()` (wall-clock, not `clock()` — CPU time would read ~0 while blocked waiting for the human's stdin input), and calls `clockConsume()` once a move is decided.
- **Lichess play** (`lichess.c`, any game with a real time control): rather than tracking its own clock, `lichess.c` *mirrors* the authoritative `wtime`/`btime`/`winc`/`binc` fields Lichess reports in every game-state update, via `clockTimeFor()`'s direct pointer access.

### Flag-fall ordering

`clockConsume(clock, side, elapsed)` subtracts `elapsed` first, and only adds the increment back if that didn't deplete the clock (returns `false` on flag-fall, withholding the increment). `main.c` calls this **before** committing a move to the board — a move that was only decided after the flag had already fallen doesn't count, matching how a physical chess clock actually works (you lose the instant your flag falls, regardless of whether you'd already found a good move). Getting this ordering right matters: an earlier version of this checked the clock *after* applying and logging the move, which meant a losing-on-time player's move still showed up in the PGN.

## Lichess Board API (`lichess.c`)

Talks to [Lichess's Board API](https://lichess.org/api#tag/Board) over HTTP (libcurl). Only compiled in with `make LICHESS=1`, keeping the rest of the project dependency-free by default. There's no chess.com support because chess.com's public API is read-only (stats/archives) — it has no live-play endpoint to talk to.

### Scope (v1)

The user creates or accepts the game on lichess.org first and passes its game ID on the command line (`--lichess <gameId>`); the engine doesn't do matchmaking (challenge creation, seeking) itself. This avoids needing the account-level event stream, only the per-game one.

### Lightweight JSON, on purpose

Rather than a real JSON library, `lichess.c` uses two tiny `strstr`-based extractors: `jsonFindString()` for quoted string fields (`"key":"value"`) and `jsonFindNumber()` for numeric ones (`"key":123`), plus `jsonFindNestedString()` to scope a search inside an outer object (needed because both `white` and `black` objects have an `id` field). This works because Lichess's NDJSON messages are shallow — a real parser would be correct in more edge cases but is unjustified complexity for this message shape. If a future feature needs deeper/more robust parsing, that's the point to reconsider.

### Streaming

`GET /api/board/game/stream/{id}` is a long-lived NDJSON stream (one JSON object per line): a `gameFull` object first, then a `gameState` object per event. `lichessStreamOnce()` does one blocking `curl_easy_perform()` against it, with a write callback (`streamWriteCallback`) that buffers partial lines and returns `0` (aborting the transfer) the moment it sees either a new move or a status change — the caller treats the resulting `CURLE_WRITE_ERROR` as success, not a real failure, since that's just the callback getting what it came for and cutting the connection. This gives synchronous, single-threaded code (`playLichessGame()`'s loop just calls `lichessStreamOnce()` again each time it needs to wait for the opponent) at the cost of true always-on streaming, which is a reasonable trade for a console client with no background threads.

### Bot mode (`--bot`)

Without `--bot`, `lichess.c` only relays moves you type (via the same `notation.c`'s `parseUserMove()` the local game loop uses) — there's no AI involvement in a normal `--lichess` session. With `--bot`, on the engine's turn it instead calls `ai.c`'s `findBestMoveTimed()` directly, using whichever `wtime`/`btime`/`winc`/`binc` the most recent stream message reported for its own color — the *actual* match clock, not a guess. If a game somehow has no time control info at all (`hasClockInfo` never set), bot mode falls back to the flat `findBestMove()`/`setSearchTimeLimit()` behavior described in [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md).
