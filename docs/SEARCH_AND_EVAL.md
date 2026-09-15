# Search & Evaluation

Covers `include/ai.h`/`src/ai.c` and `include/eval.h`/`src/eval.c`. See [README.md](README.md) for the doc index and [BOARD_AND_RULES.md](BOARD_AND_RULES.md) for the `makeMove`/`undoMove` primitives this builds on.

## Move generation

`generateAllLegalMoves(board)` is two-phase: `generatePseudoLegalMoves()` generates every geometrically-valid move per piece type (ignoring whether it leaves the mover's own king in check), then each candidate is tried with `makeMove()`/`undoMove()` and discarded if `isKingInCheck()` comes back true for the mover. This is simple but not fast — there's no legality pre-filtering (e.g. pin detection before generation) — which is fine at this engine's search depths but is the first thing to optimize if depth needs to go much higher.

An empty result means the side to move has no legal moves at all: checkmate if `isKingInCheck()` is also true, stalemate otherwise (see [BOARD_AND_RULES.md](BOARD_AND_RULES.md#draw-detection)).

## The search: iterative-deepening NegaMax

`findBestMove()` is NegaMax (every node maximizes "me minus opponent", flipping sign on each recursive call, rather than alternating explicit maximizing/minimizing players) with alpha-beta pruning, wrapped in **iterative deepening**: it searches depth 1, then 2, then 3, and so on up to `getSearchDepth()`, keeping the best move from the last depth that finished *completely* before the time cap expired.

Why iterative deepening instead of a single fixed-depth search: it's what makes the time cap (`setSearchTimeLimit()`/`findBestMoveTimed()`) meaningful. A single depth-8 search either finishes or it doesn't; there's no "good enough, ran out of time" middle ground. Iterative deepening always has *some* completed, sound result to fall back to the moment time runs out, even if it never reaches the configured depth ceiling. (It's also provably no more expensive than a single fixed-depth search once move ordering from shallower iterations feeds into deeper ones — this engine doesn't yet do that, so each depth restarts move ordering from scratch via `scoreMoves()`, but the completed-result-at-any-point property holds regardless.)

Two search-time refinements beyond plain NegaMax:

- **Quiescence search** (`quiescence()`): at the search horizon, instead of returning the static evaluation immediately, it keeps searching *captures only* until the position is "quiet" (no more captures available), to avoid the horizon effect — stopping mid-exchange and misjudging a position as fine right before losing a piece.
- **Check extension**: when `isKingInCheck()` is true at a node, `negamax()` searches one ply deeper than requested there, so the search doesn't stop right before (and fail to see) a forced mate.
- **MVV-LVA move ordering** (`scoreMove()`/`scoreMoves()`): captures are tried before quiet moves, ordered by "Most Valuable Victim, Least Valuable Aggressor" (capturing a queen with a pawn ranks above capturing a pawn with a queen), since alpha-beta pruning is only as good as how quickly it finds a strong move to prune against.

### Time management

Two independent controls, both adjustable at runtime (`main.c`'s `depth`/`time` commands, or `--depth`/`--time` flags):

- `setSearchDepth(n)` / `getSearchDepth()` — the hard ceiling on plies searched.
- `setSearchTimeLimit(seconds)` / `getSearchTimeLimit()` — a flat per-move wall-clock cap, checked periodically during the search (`searchShouldStop()`, throttled to once every `TIME_CHECK_INTERVAL` node visits so `clock()` itself doesn't become overhead) via a global `searchAborted` flag that both `negamax()` and `quiescence()` check on entry. Once set, the abort flag causes every in-flight recursive call to return immediately, unwinding the whole tree quickly; `findBestMove()` discards the just-aborted depth's (incomplete, unreliable) results and keeps the last depth that finished cleanly.

That flat cap is what's in effect when **no** real clock is running (e.g. plain local play with no `--clock`). When a real clock *is* in use (`--clock` locally, or `--bot` mode on Lichess), `main.c`/`lichess.c` call `findBestMoveTimed()` instead of `findBestMove()` directly:

```
findBestMoveTimed(board, remainingSeconds, incrementSeconds)
    = setSearchTimeLimit(computeMoveTimeBudget(board, remainingSeconds, incrementSeconds))
      then findBestMove(board), then restore the previous time limit
```

`computeMoveTimeBudget()` is the actual "smart" time allocator — the same category of heuristic real chess engines use:

1. Estimate how many moves are probably left (`movesToGo`), from 20 (bare endgame) up to 40 (full material), using `eval.h`'s `getGamePhase()` as the signal — more material on the board generally means more moves are still to come.
2. Base budget = `remainingSeconds / movesToGo`, plus 80% of the increment (banking a little in reserve rather than spending the whole increment every move).
3. **Phase factor**: ×1.2 in a materially rich middlegame (phase 8–20), ×0.7 in a near-bare endgame (phase < 4) — spend more where tactics matter most, less where play is often forced.
4. **Safety clamp**: never more than 40% of what's left on a single move.
5. **Panic mode**: below 5 seconds remaining, cut the budget to at most 20% of what's left, so the engine never flags itself.

See `ai.c`'s `computeMoveTimeBudget()` for the exact constants, and [ONLINE_PLAY.md](ONLINE_PLAY.md) for how the real clock feeds into this for local `--clock` play and Lichess `--bot` mode.

## Draws inside the search

Distinct from the *real*, game-ending draw declarations in `main.c` (see [BOARD_AND_RULES.md](BOARD_AND_RULES.md#draw-detection)) — this is purely an internal pruning heuristic. Inside `negamax()`, a node where `board.halfmoveClock >= 100` or `eval.h`'s (re-exported from `ai.h`) `isInsufficientMaterial()` is true immediately returns a draw score (0), the same way a real draw would score, so the search doesn't waste effort exploring lines it can already tell are heading toward a known-drawn outcome. It has no effect on whether the *actual game* ends — that's a separate, later check in `main.c`.

## Evaluation

`evaluateBoard(board)` (from White's perspective — positive favors White) is a **tapered** evaluation: it computes a middlegame score and an endgame score independently (material + piece-square tables + mobility, with different piece-square tables for each phase — e.g. a king wants to be tucked away in the middlegame but active in the endgame), then blends them by `getGamePhase()`:

```
score = (mgScore * phase + egScore * (24 - phase)) / 24
```

`getGamePhase(board)` counts remaining knights/bishops (1 each), rooks (2 each), and queens (4 each) — capped at 24 — as a proxy for "how much of the game's material/complexity is still on the board." It's exposed publicly (not just an internal detail of `evaluateBoard`) specifically so `ai.c`'s time management can use the same signal — see above.

`quiescence()`'s "stand-pat" score is exactly `evaluateBoard()`, sign-flipped if the side to move is Black (since `evaluateBoard` is always from White's perspective but NegaMax needs "me minus opponent").
