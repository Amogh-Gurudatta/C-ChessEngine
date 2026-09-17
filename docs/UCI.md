# UCI Protocol Support

Covers `include/uci.h`/`src/uci.c`. See [README.md](README.md) for the doc index, [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md) for what `findBestMove`/`findBestMoveTimed` actually do once called, and [NOTATION_AND_FORMATS.md](NOTATION_AND_FORMATS.md) for the FEN/long-algebraic parsing this reuses.

## What this is for

`main.c`'s REPL and `lichess.c`'s Board/Bot API client are both this engine talking to a specific thing (a human at a keyboard, or Lichess). `uci.c` is the third way to drive it: [UCI](https://en.wikipedia.org/wiki/Universal_Chess_Interface) is the de facto standard protocol nearly every chess GUI and match runner speaks — [cutechess](https://github.com/cutechess/cutechess) (both the GUI and `cutechess-cli`), Arena, and others. Running `./build/chess_engine --uci` puts the engine in this mode instead of starting the local game loop: `runUciLoop()` (in `uci.c`) takes over entirely, reading commands from stdin and writing responses to stdout, until `quit`.

**Always compiled in** — unlike `lichess.c`, it has no external dependency (it's just stdin/stdout plus the same `notation.c`/`ai.c`/`game.c` calls `main.c` already makes), so there's no `make UCI=1` opt-in.

## Using it with cutechess

In the cutechess GUI, add an engine pointing at `build/chess_engine` with `--uci` as its command-line argument (not a UCI option — this project has no `setoption`-tunable parameters, see below). For `cutechess-cli`, e.g.:

```bash
cutechess-cli -engine cmd=build/chess_engine arg=--uci name=C-ChessEngine \
              -engine cmd=/path/to/other/engine name=Other \
              -each proto=uci tc=40/60 -rounds 10
```

Any `tc`/`st` time control works, since `go`'s `wtime`/`btime`/`winc`/`binc` and `movetime` are both fully supported (see below) — this is exactly the `--clock` local-play path and the `--bot` Lichess path reusing the same `findBestMoveTimed`/`computeMoveTimeBudget` machinery, just fed by the GUI's clock instead of a wall-clock timer or Lichess's reported `wtime`/`btime`.

## Command support

| Command | Support |
| --- | --- |
| `uci` | Replies `id name C-ChessEngine`, `id author ...`, `uciok`. No declared options — there's nothing here to configure via `setoption` (depth/time are set purely from `go`'s own parameters per move, not a persistent option). |
| `isready` | Replies `readyok` immediately — the engine has no asynchronous setup to wait on. |
| `ucinewgame` | Resets the board to the standard starting position. |
| `position startpos [moves ...]` | Sets up the standard start, then replays each UCI move (long algebraic — the same format `notation.c`'s `parseLongAlgebraic`/`moveToLongAlgebraic` already use for local play and Lichess) via `resolveMove`/`makeMove`. Calls `game.h`'s `resetMoveHistory()` before replaying — see below. |
| `position fen <fen> [moves ...]` | Same, but starting from an arbitrary FEN via `notation.c`'s `fenToBoard` — tolerates a FEN missing its trailing halfmove/fullmove fields, which `fenToBoard` already treats as optional. |
| `go wtime <ms> btime <ms> [winc <ms>] [binc <ms>]` | Full real-clock support: reads whichever side's remaining time is to move, plus its increment, and calls `findBestMoveTimed` — the same time-management heuristic (`computeMoveTimeBudget`) used everywhere else in the engine. |
| `go movetime <ms>` | A flat per-move budget via `setSearchTimeLimit`. |
| `go depth <n>` | Searches to exactly that depth, **uncapped by time** — a depth-fixed match expects the full depth regardless of how long it takes, so any previously-set time limit is suspended for this one search and restored after. |
| `go` (no parameters) | Falls back to whatever depth/time the engine's own defaults are (`ai.c`'s `DEFAULT_SEARCH_DEPTH`/`DEFAULT_SEARCH_TIME_LIMIT`) — practically never hit against a real match runner, which always sends at least one of the above. |
| `quit` | Exits the loop (and the process, via `main.c` returning). |

## Why `position` resets the move-history stack every time

The UCI protocol resends the *entire* move list on every single `position` command — not just the moves since the last one — so `uciHandlePosition()` always rebuilds from scratch: reset to startpos/fen, then replay every move via `makeMove()`. That replay never calls the matching `undoMove()` (there's nothing to undo — these are real moves being committed), which matters because `game.c` tracks undo data on one shared, file-static stack (see [BOARD_AND_RULES.md](BOARD_AND_RULES.md#makemove--undomove)) that has no way to know a fresh position just got rebuilt on top of it.

Without resetting that stack before each replay, it accumulates across the *whole game* — every `position` command's full replay adds on top of every earlier one, since nothing ever pops it back down. This is exactly how a real cutechess game was lost on "illegal move": around ply 90, the accumulated, never-reset history from ~45 prior `go` commands' worth of replays finally exceeded the stack's capacity, and every `makeMove()`/`undoMove()` pair from that point on (including deep inside the search itself) silently restored the wrong data. The fix is `game.h`'s `resetMoveHistory()`, called at the start of every `position` rebuild and on `ucinewgame` — see that function's doc comment for the full mechanism.

## What's deliberately not supported

- **`stop` is a no-op.** The search (`findBestMove`/`findBestMoveTimed`) is one synchronous, blocking call — there's no background search thread for a `stop` sent mid-search to interrupt, and no separate reader thread that could even *see* `stop` arrive before the blocking call already returned `bestmove` on its own. This is fine for normal timed play (the engine already stops itself at the right time via its own time cap) but means true pondering/infinite-analysis mode isn't meaningful here.
- **`go infinite` and `ponder`** are read (so they don't get misparsed as an unknown token mid-command) but have no special effect — they fall through to the same "no time control given" default as a bare `go`, rather than actually searching forever until `stop`.
- **`setoption`** is accepted (silently ignored) — there are no tunable UCI options, since depth/time are already fully specified per-move by `go` itself.
- **`searchmoves`, `nodes`, `mate`** (as `go` sub-parameters) are read and skipped, with no effect on the search.

None of this affects a normal timed engine-vs-engine match (the only thing `cutechess-cli` actually needs): a real time control always arrives as `wtime`/`btime`/`movetime`/`depth`, all of which are genuinely supported.
