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

Five search-time refinements beyond plain NegaMax:

- **Quiescence search** (`quiescence()`): at the search horizon, instead of returning the static evaluation immediately, it keeps searching *captures only* until the position is "quiet" (no more captures available), to avoid the horizon effect — stopping mid-exchange and misjudging a position as fine right before losing a piece. It uses plain MVV-LVA ordering only (see below) — no transposition table, no killers/history; see Transposition table below for why.
- **Check extension**: when `isKingInCheck()` is true at a node, `negamax()` searches one ply deeper than requested there, so the search doesn't stop right before (and fail to see) a forced mate.
- **Null-move pruning** and **late move reductions** — see their own section below.
- **Move ordering** (`scoreMove()`/`scoreMoves()`): alpha-beta pruning is only as good as how quickly it finds a strong move to prune against, so moves are tried in this priority order at every `negamax()` node:
  1. The transposition table's suggested move for this exact position, if any (see below).
  2. Captures, by MVV-LVA ("Most Valuable Victim, Least Valuable Aggressor" — capturing a queen with a pawn ranks above capturing a pawn with a queen).
  3. Promotions.
  4. **Killer moves**: up to two quiet (non-capture) moves per ply that recently caused a beta cutoff in a *sibling* branch at that same ply. The reasoning: a quiet move that refuted one line is a good first guess for refuting a similarly-shaped sibling line too. Stored in a small fixed-size `killerMoves[ply][2]` array, reset at the start of every `findBestMove()` call (they're only meaningful within one search tree) and always bounds-checked against ply, since a very high configured depth plus stacked check extensions could in principle exceed any fixed array size.
  5. Other quiet moves, by **history heuristic**: a `historyTable[from][to]` accumulator incremented by `depth²` every time that move causes a beta cutoff anywhere in the current search (a stronger signal than a single-ply killer, at the cost of being less specific). Also reset per `findBestMove()` call, and clamped in `scoreMove()` so it can never outrank an actual killer.

### Null-move pruning

At a node where the side to move isn't in check, `negamax()` tries giving the opponent a free move (a "null move" - flip `board->currentPlayer`, don't move a piece) and searches *that* at a reduced depth with a narrow `(-beta, -beta+1)` window. If the opponent still can't reach `beta` even with a free tempo, the actual position is safely at least that good, and the whole subtree is pruned without a full search. Toggle: `setUseNullMovePruning()`/`getUseNullMovePruning()` (enabled by default).

Two safeguards keep this sound:

- **Zugzwang**: null-move pruning assumes passing can never be better than moving, which is false in zugzwang positions (common in king-and-pawn endgames, where the side to move would genuinely prefer to pass). `hasNonPawnMaterial()` skips it whenever the side to move has nothing but king and pawns left - the standard, simple mitigation (a full verification search is a further refinement real engines sometimes add; not needed at this scale).
- **No two null moves in a row**: `negamax()` takes an `allowNullMove` parameter, `true` from every normal recursive call and `false` specifically for the recursive call inside the null-move probe itself, so a second null move can't immediately follow the first.

`NULL_MOVE_MIN_DEPTH` is deliberately `NULL_MOVE_REDUCTION + 2`, not `+ 1`: that guarantees the reduced probe always retains at least one real ply of full search before falling into `quiescence()` (`depth - 1 - NULL_MOVE_REDUCTION >= 1`). A bare quiescence stand-pat is far too cheap and unreliable a "verification" on its own - measured (via a throwaway diagnostic, not part of the permanent suite) to cause node counts thousands of times smaller than correct at some depths before this margin was added, from wildly excessive false cutoffs. With the margin in place, null-move pruning gives the expected, modest textbook effect (roughly 1.5-2x fewer nodes at shallow depths, growing at deeper ones) rather than an unsound one.

Making/undoing a null move needed two small `ai.c`-local functions (`makeNullMove`/`undoNullMove`) since no existing `game.c` function represents "pass the turn" - a `Move` always represents a real piece movement. They flip `currentPlayer` and save/clear/restore `enPassantTarget` (a real move always consumes or invalidates it, so a null move must too), deliberately leaving `halfmoveClock`/`fullmoveNumber` untouched since this is a hypothetical probe, never a played move. `zobristHash()`'s existing recompute-from-scratch design (see Transposition table below) reflects both changes correctly with no extra work.

### Late move reductions (LMR)

Moves ordered late in `scoreMoves()`'s output - past the transposition table's suggestion, captures, promotions, and killers - are statistically unlikely to be the best move. Rather than searching every one of them at full depth, `negamax()` searches late, quiet, non-check moves at a reduced depth first (`LMR_MIN_MOVE_INDEX` onward, once `LMR_MIN_DEPTH` plies remain), and only pays for a full-depth re-search if that cheap probe unexpectedly beats `alpha`. Toggle: `setUseLateMoveReductions()`/`getUseLateMoveReductions()` (enabled by default).

The re-search uses the same full `(-beta, -alpha)` window as an unreduced move would, rather than a null-window probe first - that pairing (Principal Variation Search) is a natural, separate future refinement, left out to keep this simpler. Node-count reduction grows with depth (negligible at shallow depths where the move-index/depth guards rarely apply, several-times fewer nodes by depth 6 in informal testing), consistent with LMR's effect compounding through the tree the same way null-move pruning's does.

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
6. **Hard cap**: never more than `MAX_MOVE_BUDGET_SECONDS` (30s) on one move, whatever the clock says. Without it, a correspondence game (which reports *days* per move as its clock) gave a budget of ~1.8 hours per move at 3 days, and 8+ hours at 14 days - the engine appeared to think forever on Lichess.

### Stable-best-move early exit

The budget above is a *ceiling*, not a target. Under a clock (`findBestMoveTimed()` only), iterative deepening also stops early once it has settled: when the best move is unchanged, and its score has moved by no more than `STABLE_MOVE_SCORE_MARGIN` (15 centipawns), across `STABLE_MOVE_STREAK` (3) consecutive depth-to-depth comparisons, searching deeper is very unlikely to change the answer, so the engine reports it instead of burning the rest of the budget. Toggle: `setUseStableMoveEarlyExit()`/`getUseStableMoveEarlyExit()` (enabled by default).

It is deliberately conservative, because an earlier bug made the engine play far *faster* than its clock allowed (see the depth-ceiling note above), and this must not quietly bring that back:

- **Minimum depth** (`STABLE_MOVE_MIN_DEPTH`, 8): shallow depths flip-flop between candidate moves constantly, so agreement there means little.
- **Minimum budget spent** (`STABLE_MOVE_MIN_BUDGET_FRACTION`, 25%): a quiet position where depth 8 is reached in a few milliseconds still gets a real fraction of its budget before the exit can fire.
- **Forced mates skip these gates**: a winning mate score confirmed by two consecutive depths stops the search immediately - iterative deepening finds the shortest mate first, so searching deeper can't improve it.
- **Timed searches only**: `findBestMove()` (fixed depth, `go depth`, `go movetime`, the transposition-table tests, ...) always runs to completion, since those callers asked for a specific depth or time.

The score comparison uses each depth's exact root score (the root loop searches with an open window and a rising `alpha`, so the best move's value is exact, not a bound).

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
