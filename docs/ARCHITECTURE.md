# Architecture

A map of how the engine's modules fit together. See [docs/README.md](README.md) for the full documentation index.

## Directory layout

```
include/     Public headers (.h) — one per module, each with a @file docstring
src/         Implementation (.c) — one file per header, same name
tests/       Unit tests (see TESTING.md)
docs/        You are here
.github/     CI workflow (runs `make test`, `make`, `make LICHESS=1` on every push)
Makefile
README.md    User-facing: build, install, play
```

`include/` and `src/` are split by convention (headers vs. implementation), not by access control — nothing here is a "public API" in the library sense; every module can see every header via `-Iinclude`.

## Module map

```
                         structs.h
                  (Piece, Move, BoardState, ...)
                             |
              +--------------+--------------+
              |              |               |
           game.c         eval.c          fileio.c
      (rules engine)   (position score)  (legacy save format)
              |              |
              +------+-------+
                     |
                   ai.c
        (move generation + NegaMax search)
                     |
              +------+-------+
              |               |
         notation.c      timecontrol.c
     (SAN/FEN/PGN text)   (Fischer clock)
              |               |
              +------+--------+
                     |
              +------+-------+
              |               |
           main.c         lichess.c
      (console REPL)   (Lichess Board API, optional)
```

- **structs.h** defines every shared type (`Piece`, `Move`, `MoveList`, `BoardState`, ...) and nothing else — no functions, no logic. Everything else depends on it. See [BOARD_AND_RULES.md](BOARD_AND_RULES.md) for the type definitions themselves, especially the row/column coordinate convention, which is the single most important invariant in the codebase to get right.
- **game.c** is the rules engine: `makeMove`/`undoMove`, check and attack detection. It knows nothing about search or notation — just "given a move, apply it correctly."
- **eval.c** scores a position from White's perspective. It doesn't know about search either; it's a pure function of the board.
- **fileio.c** is the original save/load format (`board.txt`) plus the `pieceToChar`/`charToPiece` helpers that `notation.c` also reuses for FEN's piece-placement field. It predates `notation.c` and is kept separate deliberately — see [NOTATION_AND_FORMATS.md](NOTATION_AND_FORMATS.md) for why the two formats coexist instead of one replacing the other.
- **ai.c** is where move generation and the search live together: `generateAllLegalMoves` (pseudo-legal generation per piece type, filtered for king safety via `game.c`'s `makeMove`/`undoMove`) and `findBestMove`/`findBestMoveTimed` (iterative-deepening NegaMax with alpha-beta, quiescence, and MVV-LVA ordering, on top of `eval.c`'s scoring). See [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md).
- **notation.c** converts between `BoardState`/`Move` and every text format the engine speaks: long algebraic, SAN, FEN, PGN, and the position-key used for repetition detection. It depends on `ai.c` (to generate legal moves for SAN disambiguation and check/mate detection) and `game.c` (to simulate a move when formatting SAN). See [NOTATION_AND_FORMATS.md](NOTATION_AND_FORMATS.md).
- **timecontrol.c** is a small, self-contained Fischer clock (remaining time + increment per side), shared by local play and Lichess play. See [ONLINE_PLAY.md](ONLINE_PLAY.md).
- **main.c** is the console REPL: the game loop, command dispatch (`save`, `fen`, `undo`, `depth`, `time`, `resign`, `draw`, ...), draw-condition checks, and clock bookkeeping. It's intentionally "thin" — all the real logic lives in the modules above; `main.c` mostly wires them together and handles I/O.
- **lichess.c** (only compiled with `make LICHESS=1`) is the Lichess Board API client: HTTP via libcurl, a small hand-rolled JSON field extractor (no full parser, since the API's messages are shallow enough not to need one), and either relaying typed moves or (in `--bot` mode) calling `ai.c`'s `findBestMoveTimed` directly. See [ONLINE_PLAY.md](ONLINE_PLAY.md).

## Data flow: a human move, end to end

1. `main.c`'s game loop prints the board and prompts for input.
2. The typed string goes to `notation.c`'s `parseUserMove`, which tries long algebraic first, then SAN (`sanToMove`), calling into `ai.c`'s `generateAllLegalMoves` to validate/disambiguate against the current position.
3. On success, `main.c` calls `notation.c`'s `moveToSan` (to log the move) and then `game.c`'s `makeMove` (to actually apply it) — in that order, since `moveToSan` needs the board in its *pre-move* state.
4. `main.c` appends the new position to three parallel histories: the SAN log (for `moves`/PGN export), a position-key history (for threefold repetition, via `notation.c`'s `boardToPositionKey`), and a full FEN snapshot history (for the `undo` command — see [BOARD_AND_RULES.md](BOARD_AND_RULES.md#undo) for why `undo` uses FEN snapshots rather than `game.c`'s own undo stack).
5. Back at the top of the loop, `main.c` checks for checkmate/stalemate (`generateAllLegalMoves` returns empty) and the other draw conditions (50-move rule, insufficient material, threefold repetition) before prompting the next side.
6. If it's the engine's turn, `main.c` calls `ai.c`'s `findBestMove` (or, under a clock, `findBestMoveTimed`) instead of reading from stdin, and otherwise follows the same commit/log/history steps.

## Key invariants worth knowing before touching this code

- **Coordinates**: `row 0` is rank 8, `row 7` is rank 1 (the array is stored "as printed", top row first). See `structs.h`'s `Position` docstring.
- **`BoardState` is the one shared mutable object.** Nearly every function takes a `BoardState *` and either reads or mutates it in place; there's no copy-on-write or persistent-data-structure discipline here, so ordering (make before undo, generate-then-filter, etc.) matters.
- **`game.c`'s undo history is a single global stack, not tied to any particular `BoardState`.** It's only ever safe to call `undoMove` in a tight, balanced pair with the `makeMove` that immediately preceded it (which is exactly how `ai.c`'s search and `notation.c`'s `moveToSan` use it). It is *not* reset when a new position is loaded (`notation.c`'s `fenToBoard`), which is why `main.c`'s `undo` command restores from its own FEN-snapshot history instead of calling `undoMove` directly. See [BOARD_AND_RULES.md](BOARD_AND_RULES.md#undo).
- **Draw detection is split across two layers that don't talk to each other.** `ai.c`'s search treats the 50-move rule and insufficient material as heuristics (returns an immediate draw score, to prune the tree) — that's purely an internal search optimization. The actual, game-ending draw declarations (checkmate, stalemate, 50-move rule, insufficient material, threefold repetition) all live in `main.c`'s game loop. See [BOARD_AND_RULES.md](BOARD_AND_RULES.md#draw-detection) and [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md#draws-inside-the-search).
- **Notation vs. persistence are deliberately separate formats.** `fileio.c`'s `board.txt` format is not FEN, despite looking similar, and `notation.c`'s real FEN support doesn't replace it — see [NOTATION_AND_FORMATS.md](NOTATION_AND_FORMATS.md).

## Build system

The `Makefile` compiles every `src/*.c` file (except `lichess.c`, unless `LICHESS=1` is passed) against `include/`, with strict warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion`) treated as a correctness signal, not noise. Key targets:

- `make` — release build, `build/chess_engine`.
- `make LICHESS=1` — also links libcurl and compiles `lichess.c`, enabling `--lichess`/`--bot`.
- `make DEBUG=1` — unoptimized build with debug symbols (combinable with `LICHESS=1`).
- `make test` — builds and runs `build/run_tests` (see [TESTING.md](TESTING.md)).
- `make run` — build then execute.
- `make clean` / `make distclean` — remove build artifacts (and, for `distclean`, the saved `board.txt`).

### Generated API docs

If [Doxygen](https://www.doxygen.nl/) is installed, `make docs` generates browsable HTML from every header's docstrings into `build/docs/html/` (open `build/docs/html/index.html`). This is generated output, not checked into git — `build/` is gitignored. The config lives in `Doxyfile` at the repo root.
