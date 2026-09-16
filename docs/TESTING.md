# Testing

Covers `tests/`. See [README.md](README.md) for the doc index.

## Harness design

No external test framework — just a shared, minimal assertion macro, consistent with the project's "no dependencies unless necessary" style (see [ARCHITECTURE.md](ARCHITECTURE.md)):

- `tests/test_common.h`/`test_common.c` — the `CHECK(cond, description)` macro (increments a pass/fail counter and prints on failure) and the `SECTION(name)` macro (a printed divider), plus the two shared counters (`testsRun`, `testsFailed`).
- One file per module under test (`test_fileio.c`, `test_game.c`, `test_movegen.c`, `test_eval.c`, `test_ai.c`, `test_notation.c`, `test_timecontrol.c`), each exposing a single `void run_X_tests(void)` entry point (not `main()` — see below).
- `tests/test_main.c` — the actual `main()`, which calls every module's `run_X_tests()` in turn and prints the final `passed/total` summary.

All of these link into one binary (`build/run_tests`, built by `make test`) together with the production object files they need (`notation.o`, `ai.o`, `game.o`, `eval.o`, `fileio.o`, `timecontrol.o`) — `main.c` and `lichess.c` are deliberately excluded (see below).

### Adding a new test file

1. Create `tests/test_whatever.c`, `#include "test_common.h"`, write `static void test_xyz(void) { SECTION("..."); CHECK(...); }` functions, and one non-static `void run_whatever_tests(void)` that calls them.
2. Declare and call `run_whatever_tests()` from `tests/test_main.c`.
3. That's it — the Makefile's `TEST_SRCS := $(wildcard tests/*.c)` picks up the new file automatically; no Makefile edit needed unless the test needs a new production `.o` file linked in (add it to `TEST_OBJS`).

## What's covered

| File | Covers |
| --- | --- |
| `test_fileio.c` | `pieceToChar`/`charToPiece` round-trip for every piece/color; `saveBoardToFile`/`loadBoardFromFile` round-trip; missing-file handling |
| `test_game.c` | `makeMove`/`undoMove` for normal moves, captures, en passant, promotion, castling (both sides, both colors), castling-rights revocation on rook capture, fullmove counting, `isSquareAttacked`/`isKingInCheck` |
| `test_movegen.c` | **perft(1)=20, perft(2)=400, perft(3)=8902** from the standard starting position (see below), a pinned-piece legality check, checkmate/stalemate zero-legal-moves checks, `isInsufficientMaterial` |
| `test_eval.c` | Symmetric start position evaluates to exactly 0; material imbalance has the correct sign; `getGamePhase` at full/zero/partial material |
| `test_ai.c` | `findBestMove` finds a forced mate-in-1; `setSearchDepth`/`getSearchDepth`; `setSearchTimeLimit`/`getSearchTimeLimit`; a tight time cap interrupts a deep search and still returns a legal move; `computeMoveTimeBudget`'s qualitative behavior (more time → bigger budget, more increment → bigger budget, panic mode caps it, more game phase → more time); `findBestMoveTimed` respects a real clock end to end |
| `test_notation.c` | Long algebraic parse/format round-trip; SAN parsing and formatting (basic moves, captures, disambiguation, castling, promotion, checkmate); FEN round-trip (including the standard starting position byte-for-byte, and an en passant target); `boardToPositionKey`'s clock-independence via a simulated king-shuffle repetition; PGN export |
| `test_timecontrol.c` | `clockInit`, `clockConsume` (including the increment-after-elapsed ordering), flag-fall detection and increment withholding, `clockFormat` |

### Perft as a regression technique

[Perft](https://www.chessprogramming.org/Perft) counts the leaf nodes of the legal-move tree at a fixed depth from a known position. It's an unusually strong correctness check for a move generator: `perft(3) == 8902` from the starting position can only hold if move generation, the king-safety filter, and `makeMove`/`undoMove` are *all* correct together — a single missing en passant case, an incorrect castling right revocation, or an off-by-one in pawn double-push logic would change the count. It's the test most likely to catch a subtle regression in `game.c` or `ai.c` that a hand-picked example position would miss.

## What's *not* covered, and why

- **`lichess.c`** needs a live network connection, a real Lichess account, and an API token — not appropriate for an automated, offline test suite. It's excluded from `TEST_OBJS` entirely (and from the `LICHESS=1` build's default test run — `make test` never links libcurl).
- **`main.c`'s I/O loop itself** (the REPL, command dispatch, argv parsing) is thin glue over the modules that *are* tested — reading a line, dispatching to a well-tested function, printing the result. It's exercised manually (see the [README](../README.md#gameplay--commands) for the commands) rather than through the automated suite.
- **`uci.c`** is the same kind of thin glue as `main.c`'s I/O loop, just over a different protocol (see [UCI.md](UCI.md)) — it parses a command, calls an already-tested `notation.c`/`ai.c` function, and prints the result. Verified manually by piping raw UCI commands to `./build/chess_engine --uci` and checking the responses, rather than through the automated suite.

## Running

```bash
make test
```

Builds `build/run_tests` and runs it, printing a `passed/total` summary. Also wired into CI (`.github/workflows/ci.yml`) on every push and pull request, alongside a plain `make` and a `make LICHESS=1` build check.
