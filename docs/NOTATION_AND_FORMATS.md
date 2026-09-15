# Notation & File Formats

Covers `include/notation.h`/`src/notation.c` and `include/fileio.h`/`src/fileio.c`. See [README.md](README.md) for the doc index.

## Why two "save format" modules

`fileio.c` predates `notation.c` in this project's history: it's a simple, custom, line-based text format (`board.txt`) — 8 rows of piece characters, then side to move, castling rights, en passant square, halfmove clock, fullmove number, one field per line. The README used to (incorrectly) call this "FEN-like"; it looks similar but **isn't** real FEN — no `/` rank separators, no run-length-encoded empty squares, and no single-line format.

`notation.c` later added real, spec-correct FEN (`boardToFen`/`fenToBoard`) for interoperability with other chess tools. Rather than replace `fileio.c`'s format and risk breaking existing `save`/`load` behavior, the two coexist: `save`/`load` (and `board.txt`) still use `fileio.c`'s format; `fen`/`loadfen` use `notation.c`'s real FEN. They do share code, though — `notation.c`'s FEN piece-placement field reuses `fileio.c`'s `pieceToChar`/`charToPiece`.

## Move notation

Three ways to specify a move, all converging on the same `Move` struct:

- **Long algebraic** (`"e2e4"`, `"a7a8q"`) — the engine's original, simplest format: source square + destination square + optional promotion letter. `parseLongAlgebraic()` parses the raw squares (without knowing whether it's e.g. really castling); `resolveMove()` then matches that raw from/to against the position's actual legal moves (via `ai.c`'s `generateAllLegalMoves()`) to recover the correct `flag` and, for an unspecified promotion, defaults to queen.
- **Standard Algebraic Notation / SAN** (`"e4"`, `"Nf3"`, `"O-O"`, `"exd5"`, `"e8=Q"`, `"Qxe7+"`) — `sanToMove()` handles the full grammar:
  - Castling (`O-O`/`O-O-O`, or `0-0`/`0-0-0` — both normalized before matching) is checked first and short-circuits everything else.
  - A leading uppercase letter (`N`/`B`/`R`/`Q`/`K`) names the piece; its absence means a pawn move.
  - **Disambiguation**: when more than one piece of that type could legally reach the destination, SAN requires a file, a rank, or both, prefixed before the destination (e.g. `Nbd2` when two knights could both reach `d2`). `sanToMove()` parses whichever of those is present and filters the legal-move list accordingly; it deliberately does *not* guess when a move is ambiguous without disambiguation — it fails (returns `false`) rather than picking arbitrarily.
  - A capture `x` is accepted but not required to match reality (lenient parsing) — except for pawn captures, where the leading file letter (`e` in `exd5`) is mandatory and *is* the disambiguation.
  - `=Q`/`=N`/etc. specifies a promotion piece; omitted on a promoting move, it defaults to queen (matching long algebraic's behavior).
  - Trailing `+`/`#` are accepted and ignored on input (not validated against whether the move is actually a check/mate) — but see `moveToSan()` below for how they're produced correctly on output.
  - `sanToMove()`'s only source of truth for "what's legal" is `ai.c`'s `generateAllLegalMoves()`, run fresh on every call — it never re-derives legality itself.
- **`moveToSan()`** (formatting, the reverse direction) computes disambiguation by scanning the legal-move list for same-type moves to the same destination with a different source square, then determines check/checkmate by actually applying the move (`game.c`'s `makeMove()`), checking `isKingInCheck()` and `generateAllLegalMoves()`'s count on the resulting position, and then undoing it (`undoMove()`) to restore the board — this is one of the few places outside `ai.c`'s search that relies on the make/undo pairing being tight and balanced (see [BOARD_AND_RULES.md](BOARD_AND_RULES.md#makemove--undomove)).

`parseUserMove()` is the dispatcher every input path (the console REPL in `main.c`, and Lichess play in `lichess.c`) actually calls: it uses `isLongAlgebraicFormat()` to decide which parser to try first (long algebraic's fixed 4-5 character shape never collides with SAN's grammar), then falls back to SAN.

## FEN

`boardToFen()`/`fenToBoard()` implement the standard six-field FEN: piece placement (rank 8 to rank 1, `/`-separated, digits for empty-square runs), side to move, castling availability, en passant target square, halfmove clock, fullmove number. Round-trips are byte-exact (verified by `tests/test_notation.c` against both the standard starting position and a mid-game position with reduced castling rights and an en passant target).

### Position keys (for repetition)

`boardToPositionKey()` is FEN's first four fields *only* — piece placement, side to move, castling rights, en passant square — deliberately omitting the halfmove/fullmove counters. Two positions that are "the same" for FIDE's threefold-repetition rule can (and after any move, always do) have different move counters, so a full FEN string would never match itself twice; the position key is what actually gets compared in `main.c`'s repetition-history tracking. See [BOARD_AND_RULES.md](BOARD_AND_RULES.md#draw-detection).

## PGN

`exportPgn()` writes the standard seven-tag roster (`Event`/`Site`/`Date`/`Round`/`White`/`Black`/`Result`) followed by numbered SAN move text, wrapped at 80 columns. It takes a pre-built SAN log rather than a `BoardState` — `main.c` accumulates that log by calling `moveToSan()` on every move *before* applying it (since `moveToSan` needs the pre-move position), in lockstep with the position-key and FEN-snapshot histories described above.
