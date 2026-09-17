# Opening Book

Covers `include/book.h`/`src/book.c`. See [README.md](README.md) for the doc index and [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md) for the search this lets the engine skip when a position is already known theory.

## What it does

`findBestMove()` (`ai.c`) checks `book.c`'s `findBookMove()` before doing any search at all. If the current position matches one of the engine's ~20 curated opening lines, it plays that line's next move immediately — instantly, and correctly, since it's established theory rather than a still-shallow search result. The moment the game goes off any known line, `findBookMove()` simply returns false and search takes over exactly as before; there's no partial/fuzzy matching, just an exact position match or nothing.

This applies everywhere `findBestMove`/`findBestMoveTimed` is called — local play, Lichess (`--bot`), and UCI (`--uci`, e.g. cutechess) — with no per-caller wiring, since it's a single check inside the one shared entry point they all use.

## The book's data

Each line in `book.c` is a plain array of moves in long algebraic notation (`"e2e4"`, the same format used everywhere else in this engine), e.g.:

```c
static const char *const ruyLopez[] = {
    "e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6", NULL};
```

The repertoire covers ~20 well-known named openings (Italian, Ruy Lopez, Scotch, Petrov, Philidor, Vienna, both Sicilian branches, French, Caro-Kann, Scandinavian, both Queen's Gambit branches, Slav, King's Indian, Nimzo-Indian, Grünfeld, Dutch, London System, English, Réti), each 5–10 plies deep — enough to get the engine safely into a known, healthy structure before it needs to start thinking for itself. Every line was verified move-by-move against the engine's own `generateAllLegalMoves()` (a throwaway checker, not part of the permanent test suite) before being added, since a single wrong square in a hand-typed line would otherwise just silently never match anything rather than fail loudly.

**Adding a new line** is just adding another array like the one above and one entry in `book.c`'s `bookLines[]` list — no other code changes needed.

## Why every book move is deliberately unremarkable

Every line above stays within ordinary piece development and pawn moves — no castling, no promotion, no en passant capture. This is a deliberate scope limit, not an oversight: `game.c`'s `makeMove()` needs a move's `flag` (`MOVE_CASTLE_KING`, `MOVE_EN_PASSANT`, ...) to be already correctly resolved to do the right thing, and *resolving* that flag from a bare "e1g1"-style string is exactly what `notation.c`'s `resolveMove()` already does — by matching it against `ai.c`'s `generateAllLegalMoves()`. Reaching for either of those from `book.c` would create a dependency cycle (`book.c` is called *by* `ai.c`, and `notation.c` itself depends on `ai.c`).

Keeping book lines to moves that are unambiguously `MOVE_NORMAL` (a plain move or an ordinary capture — `makeMove()` handles both identically) sidesteps this entirely: `book.c` only ever needs `game.h`'s `makeMove()` to build its lookup table, and `fileio.h`'s `pieceToChar()`/`charToPiece()` for position keys and its own standalone starting-position setup. No dependency on `ai.h` or `notation.h` at all. Since real opening theory essentially never castles or captures en passant within the first 4–5 full moves anyway, this costs nothing in practice — it just means a line is truncated right before its first castling move rather than including it (e.g. the Ruy Lopez line above stops at 8...Nf6, one move short of White's usual `O-O`).

`ai.c`'s side of the contract still gets the safety net for free: after `findBookMove()` returns a raw from/to square pair, `findBestMove()` re-matches it against the position's actual `generateAllLegalMoves()` result before ever returning it (see `ai.c`, right after the opening book check) — so even if a book entry's position key ever collided with a position it wasn't built for, the engine could still never play an illegal move because of it.

## How a position is matched

`book.c` builds its lookup table once, lazily, on first use: it replays each curated line from a standalone standard starting position (its own copy, independent of `main.c`'s/`lichess.c`'s — see the file comment in `book.c` for why it doesn't reach for `notation.c`'s `fenToBoard()` instead), recording a key for the position *before* each move alongside that move.

The key is the 64 squares' `pieceToChar()` values plus a side-to-move character — deliberately *not* full FEN (no castling rights, no en passant target). This means two different move orders that transpose into an identical piece arrangement get matched as the same book position, which is exactly correct chess behavior (that's what "transposition" means) and falls out of this key for free. The tradeoff: in the vanishingly rare case where two different histories reach identical piece placement but different castling rights, the book can't tell them apart — not a real concern for lines this short, where castling rights are essentially always still "everything available" for both sides anyway.

A cheap `board->fullmoveNumber` guard (comfortably above the deepest line's depth) skips the table scan entirely once a game is well past any book coverage, so this never costs anything in the middlegame or endgame.

## Determinism, not randomness

Several lines share a common prefix with different continuations (e.g. after 1.e4, different lines suggest e7e5, c7c5, e7e6, c7c6, or d7d5 as Black's reply). `findBookMove()` always returns the *first* matching entry in table order rather than picking randomly among them. This is a deliberate choice consistent with the rest of the engine (see [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md#transposition-table)'s fixed-seed Zobrist table for the same reasoning): a fully deterministic engine is easier to test and reason about, and the practical variety a human or opponent engine actually experiences comes from *their* choices diverging into different lines, not from this engine randomizing its own.

## Toggling it

`setUseOpeningBook(bool)`/`getUseOpeningBook()` enable/disable the book (on by default). There's no REPL command or CLI flag wired up for this yet — it's exposed purely as an API for now, mainly so tests can force real search behavior on book-covered positions (see `tests/test_ai.c`'s two transposition-table tests, which explicitly disable the book before running, since both use start-of-game positions that the book itself would otherwise intercept).

## Testing

Unlike `main.c`/`lichess.c`/`uci.c`, `book.c` is pure, deterministic, and side-effect-free, so it's fully covered by the automated suite (`tests/test_book.c`, see [TESTING.md](TESTING.md)): matching the starting position, following a known line move by move, missing an unrelated (endgame) position, the enable/disable toggle, and confirming `findBestMove()` returns the book's move without visiting a single search node.
