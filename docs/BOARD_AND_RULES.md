# Board Representation & Rules

Covers `include/structs.h` and `include/game.h`/`src/game.c`. See [README.md](README.md) for the doc index and [ARCHITECTURE.md](ARCHITECTURE.md) for how this fits into the rest of the engine.

## Coordinates

A `Position` is a `{row, col}` pair, **not** a rank/file pair — but the two are related by a fixed convention used everywhere in this codebase:

```
row = 8 - rank        col = file - 'a'
```

So `row 0` is rank 8 (Black's back rank) and `row 7` is rank 1 (White's back rank); the board array is stored top-to-bottom exactly as it's printed to the console. `Position{-1, -1}` is the "no such square" sentinel (used for `enPassantTarget` when no en passant capture is available, and by `notation.c`'s parsers to signal a failed parse).

Get this convention wrong and everything downstream is subtly broken in a way that's easy to miss in testing (moves that "usually" work but fail near an edge, or castling that looks right for one side and mirrored-wrong for the other) — it's worth internalizing before touching `game.c` or `ai.c`.

## Core types

- `Piece {type, color}` — a piece occupying a square, or `{EMPTY, NO_COLOR}` for an empty one.
- `Move {from, to, promotion, flag}` — `flag` is one of `MOVE_NORMAL`/`MOVE_PROMOTION`/`MOVE_EN_PASSANT`/`MOVE_CASTLE_KING`/`MOVE_CASTLE_QUEEN`. A `Move` is only fully meaningful once its flag has been *resolved* against a real position — `notation.c`'s `resolveMove()` does this for human input, since e.g. `"e1g1"` alone doesn't say whether it's an ordinary king move or castling.
- `CastlingRights {wk, wq, bk, bq}` — whether each side still has the *right* to castle on each side (king and that rook have never moved, or the opponent's rook hasn't been captured on its home square). This is necessary but not sufficient for castling being legal right now (see below).
- `BoardState` — the complete position: the 8×8 `squares` array, `currentPlayer`, `castling`, `enPassantTarget`, `halfmoveClock`, `fullmoveNumber`. Nearly every function in the engine takes a `BoardState *` and reads or mutates it directly.

## `makeMove` / `undoMove`

`game.c` keeps a **single file-static history stack** (`historyStack`/`historyTop`, 65536 entries), not tied to any particular `BoardState`. Each `makeMove()` call pushes a `MoveRecord` (captured piece, previous castling rights, previous en passant target, previous halfmove/fullmove counters, previous side to move) onto it; `undoMove()` pops the most recent record and restores exactly that state.

This has one sharp edge: **the stack is never reset by loading a new position on its own.** `notation.c`'s `fenToBoard()` overwrites a `BoardState` directly without touching the stack, so anything that rebuilds a position from scratch and keeps using `makeMove()`/`undoMove()` afterward must call `game.h`'s `resetMoveHistory()` itself, or the stack ends up in a state that has nothing to do with the position now on the board. This shows up two different ways:

- **Calling `undoMove()` after loading an unrelated position** without a reset first: it "restores" state left over from before the load — the failure mode this section originally documented.
- **Calling `makeMove()` repeatedly with no matching `undoMove()`, across many separate reloads, without ever resetting in between.** This is exactly what a UCI front end's `position ... moves ...` handler does: the protocol resends the *entire* move list on every single command (not incremental deltas), so replaying it is nothing but `makeMove()` calls with no matching `undoMove()`. Without a reset each time, the stack accumulates across the *whole session* — every "position" command's replay adds on top of every earlier one, nothing ever pops it back down. A game long enough eventually exhausts the stack, and every `makeMove()`/`undoMove()` pair from that point on (including deep inside `ai.c`'s otherwise perfectly balanced search recursion) desyncs: `undoMove()` restores a stale, unrelated `MoveRecord` instead of the one that was actually just made. The position silently corrupts rather than erroring, and the first *visible* symptom can be many moves later, once the corrupted board finally produces a move `generateAllLegalMoves()` on the *real* board would never have offered - which is exactly how this was found: a real cutechess game lost on "illegal move" after ~90 plies, root-caused by replaying the same growing move list on every one of ~45 `go` commands with no reset between them, in a single long-running `--uci` process (see [UCI.md](UCI.md)).

`ai.c`'s search and `notation.c`'s `moveToSan()` never needed to worry about either failure mode, because they only ever use `makeMove`/`undoMove` in tight, balanced pairs (`makeMove(); ...; undoMove();`) that never span a position reload in the first place - the risk is specific to callers that *rebuild from scratch and keep going*, which is `uci.c`'s `uciHandlePosition()`/`ucinewgame` handling (now calling `resetMoveHistory()` at every such rebuild) and nothing else in this codebase today.

### Undo

`main.c`'s `undo` command deliberately does **not** call `game.c`'s `undoMove()`, for the first failure mode above — after a `loadfen`, the global stack may hold moves from before the load. Instead, `main.c` keeps its own array of full FEN snapshots (`fenHistory`, one per move played since the game or a `loadfen` began) and restores by calling `notation.c`'s `fenToBoard()` on an earlier snapshot. This is slightly more expensive (a full FEN parse instead of an in-place field restore) but sidesteps the stack entirely and composes correctly with `loadfen` resetting the snapshot history.

### Special moves inside `makeMove`

- **Castling** moves both the king and the corresponding rook, clears both of that side's castling rights, and clears the en passant target.
- **En passant** removes the captured pawn from the square *behind* the destination (not the destination itself), using `currentPlayer` (captured before the turn flips) to know which direction "behind" is.
- **Promotion** replaces the pawn with the piece named in `move.promotion` on arrival at the back rank.
- **Castling rights** are also revoked incidentally: moving a rook off its home square, moving the king at all, or capturing an enemy rook on *its* home square all clear the relevant right(s) — this is checked unconditionally on every move, not just castling moves.
- **Halfmove clock** resets on any capture or pawn move, otherwise increments; **fullmove number** increments after Black moves.

## Check & attack detection

`isSquareAttacked(board, r, c, attackerColor)` is the core primitive: it checks sliding pieces (rook/bishop/queen) along all 8 directions until blocked, knight jumps, pawn attacks (direction-aware by color), and king adjacency. `isKingInCheck(board, kingColor)` is just `isSquareAttacked` on that king's current square by the opposing color. Both `ai.c`'s legal-move filter and `game.c`'s own castling-through-check rule build on this.

## Draw detection

Split across two layers on purpose — see [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md#draws-inside-the-search) for the search-time heuristic side. The **real**, game-ending draw declarations all live in `main.c`'s game loop, checked once per turn before the side to move acts:

| Condition | How it's detected |
| --- | --- |
| Checkmate / stalemate | `ai.c`'s `generateAllLegalMoves()` returns zero moves; distinguished by `isKingInCheck()` |
| 50-move rule | `board.halfmoveClock >= 100` |
| Insufficient material | `ai.c`'s `isInsufficientMaterial()` — a simplified check (see its docstring for exactly what it does and doesn't recognize) |
| Threefold repetition | `notation.c`'s `boardToPositionKey()` (piece placement + side to move + castling rights + en passant square, deliberately *excluding* the halfmove/fullmove counters, since those change every move and would otherwise prevent two occurrences of "the same" position from ever matching) recorded in a history array and counted |

`resign` and `draw` (an offer the engine evaluates via `eval.c`'s `evaluateBoard()` and accepts unless clearly ahead) are player-initiated, not rule-detected, but end the game the same way.

Time forfeits (`--clock`) are a third, independent way to end the game — see [ONLINE_PLAY.md](ONLINE_PLAY.md).
