# Search & Evaluation

Covers `include/ai.h`/`src/ai.c` and `include/eval.h`/`src/eval.c`. See [README.md](README.md) for the doc index and [BOARD_AND_RULES.md](BOARD_AND_RULES.md) for the `makeMove`/`undoMove` primitives this builds on.

## Move generation

`generateAllLegalMoves(board)` is two-phase: `generatePseudoLegalMoves()` generates every geometrically-valid move per piece type (ignoring whether it leaves the mover's own king in check), then each candidate is tried with `makeMove()`/`undoMove()` and discarded if `isKingInCheck()` comes back true for the mover. This is simple but not fast — there's no legality pre-filtering (e.g. pin detection before generation) — which is fine at this engine's search depths but is the first thing to optimize if depth needs to go much higher.

An empty result means the side to move has no legal moves at all: checkmate if `isKingInCheck()` is also true, stalemate otherwise (see [BOARD_AND_RULES.md](BOARD_AND_RULES.md#draw-detection)).

## Opening book

Before any of the below runs at all, `findBestMove()` checks `book.c`'s built-in opening book (`findBookMove()`) and, if the current position is known theory, plays its suggested move immediately - no search needed. See [OPENING_BOOK.md](OPENING_BOOK.md) for how the book is represented and matched.

## The search: iterative-deepening NegaMax

`findBestMove()` is NegaMax (every node maximizes "me minus opponent", flipping sign on each recursive call, rather than alternating explicit maximizing/minimizing players) with alpha-beta pruning, wrapped in **iterative deepening**: it searches depth 1, then 2, then 3, and so on up to `getSearchDepth()`, keeping the best move from the last depth that finished *completely* before the time cap expired.

Why iterative deepening instead of a single fixed-depth search: it's what makes the time cap (`setSearchTimeLimit()`/`findBestMoveTimed()`) meaningful. A single depth-8 search either finishes or it doesn't; there's no "good enough, ran out of time" middle ground. Iterative deepening always has *some* completed, sound result to fall back to the moment time runs out, even if it never reaches the configured depth ceiling. It also actively feeds each depth's result into the next: after a depth finishes, `findBestMove()` re-sorts the root move list with that depth's best move first (see Move ordering below), so alpha-beta at the next, deeper iteration starts from an already-strong guess instead of re-discovering it from scratch.

Three search-time refinements beyond plain NegaMax:

- **Quiescence search** (`quiescence()`): at the search horizon, instead of returning the static evaluation immediately, it keeps searching *captures only* until the position is "quiet" (no more captures available), to avoid the horizon effect — stopping mid-exchange and misjudging a position as fine right before losing a piece. It uses plain MVV-LVA ordering only (see below) — no transposition table, no killers/history; see Transposition table below for why.
- **Check extension**: when `isKingInCheck()` is true at a node, `negamax()` searches one ply deeper than requested there, so the search doesn't stop right before (and fail to see) a forced mate.
- **Move ordering** (`scoreMove()`/`scoreMoves()`): alpha-beta pruning is only as good as how quickly it finds a strong move to prune against, so moves are tried in this priority order at every `negamax()` node:
  1. The transposition table's suggested move for this exact position, if any (see below).
  2. Captures, by MVV-LVA ("Most Valuable Victim, Least Valuable Aggressor" — capturing a queen with a pawn ranks above capturing a pawn with a queen).
  3. Promotions.
  4. **Killer moves**: up to two quiet (non-capture) moves per ply that recently caused a beta cutoff in a *sibling* branch at that same ply. The reasoning: a quiet move that refuted one line is a good first guess for refuting a similarly-shaped sibling line too. Stored in a small fixed-size `killerMoves[ply][2]` array, reset at the start of every `findBestMove()` call (they're only meaningful within one search tree) and always bounds-checked against ply, since a very high configured depth plus stacked check extensions could in principle exceed any fixed array size.
  5. Other quiet moves, by **history heuristic**: a `historyTable[from][to]` accumulator incremented by `depth²` every time that move causes a beta cutoff anywhere in the current search (a stronger signal than a single-ply killer, at the cost of being less specific). Also reset per `findBestMove()` call, and clamped in `scoreMove()` so it can never outrank an actual killer.

### Time management

Two independent controls, both adjustable at runtime (`main.c`'s `depth`/`time` commands, or `--depth`/`--time` flags):

- `setSearchDepth(n)` / `getSearchDepth()` — the hard ceiling on plies searched.
- `setSearchTimeLimit(seconds)` / `getSearchTimeLimit()` — a flat per-move wall-clock cap, checked periodically during the search (`searchShouldStop()`, throttled to once every `TIME_CHECK_INTERVAL` node visits so `clock()` itself doesn't become overhead) via a global `searchAborted` flag that both `negamax()` and `quiescence()` check on entry. Once set, the abort flag causes every in-flight recursive call to return immediately, unwinding the whole tree quickly; `findBestMove()` discards the just-aborted depth's (incomplete, unreliable) results and keeps the last depth that finished cleanly.

That flat cap is what's in effect when **no** real clock is running (e.g. plain local play with no `--clock`). When a real clock *is* in use (`--clock` locally, `--bot` mode on Lichess, or a UCI `go` with `wtime`/`btime` — see [UCI.md](UCI.md)), the caller uses `findBestMoveTimed()` instead of `findBestMove()` directly:

```
findBestMoveTimed(board, remainingSeconds, incrementSeconds)
    = setSearchTimeLimit(computeMoveTimeBudget(board, remainingSeconds, incrementSeconds))
      then, if the current depth ceiling is below TIMED_SEARCH_MAX_DEPTH (64), raise it there
      then findBestMove(board), then restore the previous time limit and depth ceiling
```

The depth-ceiling bump matters: `getSearchDepth()` defaults to a plies ceiling (6) chosen for a quick response with no clock at all, and on modern hardware a depth-6 search often finishes in a small fraction of a second — well before any real time budget is used up. Without raising the ceiling too, `findBestMove()`'s iterative deepening would simply run out of depths to try and return immediately, leaving most of the computed budget (and therefore most of the engine's actual thinking) unused — the exact bug this fixed: the engine visibly finishing moves far faster than its opponent under a real clock, retaining much more time than it should. `TIMED_SEARCH_MAX_DEPTH` is chosen high enough that the *time* cap, not this ceiling, is what should realistically stop a timed search, while staying safely below `MAX_KILLER_PLY` (128, see Transposition table below) even with some check-extension stacking.

`computeMoveTimeBudget()` is the actual "smart" time allocator — the same category of heuristic real chess engines use:

1. Estimate how many moves are probably left (`movesToGo`), from 20 (bare endgame) up to 40 (full material), using `eval.h`'s `getGamePhase()` as the signal — more material on the board generally means more moves are still to come.
2. Base budget = `remainingSeconds / movesToGo`, plus 80% of the increment (banking a little in reserve rather than spending the whole increment every move).
3. **Phase factor**: ×1.2 in a materially rich middlegame (phase 8–20), ×0.7 in a near-bare endgame (phase < 4) — spend more where tactics matter most, less where play is often forced.
4. **Safety clamp**: never more than 40% of what's left on a single move.
5. **Panic mode**: below 5 seconds remaining, cut the budget to at most 20% of what's left, so the engine never flags itself.

See `ai.c`'s `computeMoveTimeBudget()` for the exact constants, and [ONLINE_PLAY.md](ONLINE_PLAY.md) for how the real clock feeds into this for local `--clock` play and Lichess `--bot` mode.

## Transposition table

A cache, keyed by a [Zobrist hash](https://www.chessprogramming.org/Zobrist_Hashing) of the position, of what `negamax()` already learned about positions it's seen before — the same position often recurs via a different move order (a *transposition*), and without this the search would redo that work every time. Toggle it with `setUseTranspositionTable()`/`getUseTranspositionTable()` (on by default); `getLastSearchNodeCount()` reports how many nodes the most recent search visited, mainly so the table's effect is actually observable (`tests/test_ai.c` uses both to confirm the table changes *speed*, never the *result*, of a completed search).

**The hash is recomputed from scratch on every node** (`zobristHash()`, a single pass over the 64 squares plus side-to-move/castling/en-passant), rather than maintained incrementally inside `makeMove()`/`undoMove()`. Incremental maintenance is the more common approach in stronger engines and would be faster per node, but it means threading a hash update through every one of `makeMove`'s special cases (castling, en passant, promotion, a captured rook revoking castling rights, ...) — if any one of those silently drifts out of sync, the result is a wrong-but-plausible search that's extremely hard to notice, let alone debug. Recomputing from scratch is a deliberate simplicity-over-speed tradeoff given the engine's scale. The Zobrist random table itself is generated once from a **fixed seed** (a small splitmix64 PRNG), not `time()`-seeded, so search results — and therefore test behavior — stay fully reproducible.

**Mate scores need special handling.** `negamax()`'s checkmate base case returns `-MATE_VALUE + ply`, which encodes "how many plies from the root of *this* search call" — meaningless once cached, since the same position reached at a different ply (a different move order, or an entirely later search) would wrongly inherit a mate distance measured from the wrong root. `mateScoreToTT()`/`mateScoreFromTT()` convert to/from a ply-independent form around every store/lookup — the standard fix for this well-known transposition-table pitfall. Getting this wrong is a classic, easy-to-miss source of engines that occasionally "forget" a mate it already knew about, or misjudge how fast one is; `tests/test_ai.c` re-runs the mate-in-one scenario at a much greater depth specifically to exercise this path harder.

Replacement uses a **depth-preferred** strategy (`ttStore()`): a shallower re-search of a position already in the table never overwrites a deeper, more valuable entry there, even though the table (2^18 entries, ~12MB, sized so an index is a cheap bitmask instead of a modulo) means unrelated positions do sometimes collide on the same slot.

## Draws inside the search

Distinct from the *real*, game-ending draw declarations in `main.c` (see [BOARD_AND_RULES.md](BOARD_AND_RULES.md#draw-detection)) — this is purely an internal pruning heuristic. Inside `negamax()`, a node where `board.halfmoveClock >= 100` or `eval.h`'s (re-exported from `ai.h`) `isInsufficientMaterial()` is true immediately returns a draw score (0), the same way a real draw would score, so the search doesn't waste effort exploring lines it can already tell are heading toward a known-drawn outcome. It has no effect on whether the *actual game* ends — that's a separate, later check in `main.c`.

## Evaluation

`evaluateBoard(board)` (from White's perspective — positive favors White) is a **tapered** evaluation: it computes a middlegame score and an endgame score independently (material + piece-square tables + mobility, with different piece-square tables for each phase — e.g. a king wants to be tucked away in the middlegame but active in the endgame), then blends them by `getGamePhase()`:

```
score = (mgScore * phase + egScore * (24 - phase)) / 24
```

`getGamePhase(board)` counts remaining knights/bishops (1 each), rooks (2 each), and queens (4 each) — capped at 24 — as a proxy for "how much of the game's material/complexity is still on the board." It's exposed publicly (not just an internal detail of `evaluateBoard`) specifically so `ai.c`'s time management can use the same signal — see above.

`quiescence()`'s "stand-pat" score is exactly `evaluateBoard()`, sign-flipped if the side to move is Black (since `evaluateBoard` is always from White's perspective but NegaMax needs "me minus opponent").

### Structural bonuses

Beyond material, piece-square tables, and mobility, `evaluateBoard()` adds a handful of positional terms recognized by name in most chess literature. Each is computed once per side by a dedicated (file-static) helper and added for White, subtracted for Black — exactly the same symmetric pattern as the main per-square loop, which is why the standard starting position still evaluates to exactly 0 with all of these active (verified by `tests/test_eval.c`, along with a dedicated, carefully-controlled test per bonus below):

- **Bishop pair** (`countBishops()`): a flat bonus when a side still has both its bishops — two bishops together cover both square colors, generally stronger than a bishop and a knight of otherwise equal value.
- **Rook file bonus** (`rookFileScore()`): a bonus for a rook on a file with no pawns at all (open) or none of its own (semi-open, smaller bonus) — an open file gives a rook unobstructed control of it.
- **Pawn structure** (`pawnStructureScore()`, using per-file pawn counts from `countPawnFiles()`):
  - *Doubled* — a penalty for each pawn beyond the first sharing a file with another of the same color (structurally weak, can't defend each other).
  - *Isolated* — a penalty for a pawn with no friendly pawn on either adjacent file (no pawn can ever defend it).
  - *Passed* (`pawnIsPassed()`) — a bonus, scaling steeply with how far advanced the pawn is (bigger in the endgame than the middlegame — a passed pawn's promotion threat matters most when there are fewer pieces left to stop it), for a pawn with no enemy pawn on its own file or either adjacent file still ahead of it toward promotion.
- **King safety** (`kingShieldScore()`): a simple pawn-shield proxy — a bonus per friendly pawn still on the rank directly in front of the king, across its file and the two adjacent ones. Added **only** to the middlegame score, not the endgame one, so it's automatically suppressed by the tapering blend as the game simplifies (see the formula above) — king safety genuinely matters less once most pieces are off the board, and king activity, already captured by `king_eg`'s piece-square table, takes over instead.
