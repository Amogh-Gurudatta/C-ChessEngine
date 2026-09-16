# **C-ChessEngine**

[![CI](https://github.com/Amogh-Gurudatta/C-ChessEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/Amogh-Gurudatta/C-ChessEngine/actions/workflows/ci.yml)

A high-performance, console-based chess engine written entirely in C. This project showcases efficient board representation, legal move generation, and advanced search algorithms including **NegaMax with Alpha-Beta Pruning**, **Quiescence Search**, and **Tapered Evaluation**.

This README covers building, installing, and playing. For how the engine works internally — module architecture, the search, notation parsing, online play, testing — see **[docs/](docs/README.md)**.

---

## **Key Features**

* **Console-Based Interface:** Simple text-based input for playing moves and executing commands.
* **Modular Architecture:** Clean separation of game logic, move generation, search, evaluation, and file I/O—making the engine suitable for future UCI integration.
* **Advanced Search Algorithms:**

  * **NegaMax + Alpha-Beta Pruning** for efficient game‑tree search
  * **Iterative Deepening** with an optional time cap, so the engine can stop and return its best move so far instead of searching indefinitely
  * **Transposition Table** (Zobrist hashing) to skip re-searching positions already seen via a different move order
  * **Quiescence Search** to reduce the horizon effect
  * **Move ordering**: the transposition table's suggested move, then MVV-LVA captures, promotions, killer moves, and the history heuristic
* **Adjustable Difficulty:** Search depth and/or a per-move time budget can be set from the command line or mid-game.
* **Real Chess Clocks:** Fischer-style clocks (time + increment) for both sides, with the engine managing its own thinking time based on time left, increment, and game phase — like a real chess engine, not a flat per-move cap.
* **Tapered Evaluation:** Material, piece-square tables, mobility, bishop pair, rook file bonuses, pawn structure (doubled/isolated/passed), and king safety, blended between **Middlegame (MG)** and **Endgame (EG)** weights based on remaining material.
* **Game Persistence:** Save and load game states through a simple `board.txt` file.
* **Full Draw Detection:** Checkmate, stalemate, the 50-move rule, insufficient material, and threefold repetition are all detected and end the game automatically.
* **Standard Notation:** Accepts and prints Standard Algebraic Notation (`e4`, `Nf3`, `O-O`), and can import/export real FEN and PGN.
* **Lichess Board API (optional):** Play a live Lichess game from the terminal (see [Playing on Lichess](#playing-on-lichess)).
* **UCI Support:** Speaks enough of the [UCI protocol](https://en.wikipedia.org/wiki/Universal_Chess_Interface) (`--uci`) to be run from a standard chess GUI or match runner like [cutechess](https://github.com/cutechess/cutechess) instead of this project's own REPL (see [Using a UCI GUI](#using-a-uci-gui)).

---

## **Quick Start**

```bash
make
./build/chess_engine
```

You play **White**; the engine plays **Black**. Type a move (`e4` or `e2e4`), then `quit` whenever you want to stop — that's the entire interface. To play Black instead (the engine moves first), add `--side black`. Everything below is optional extras.

---

## **Getting Started**

### **Prerequisites**

Ensure you have the following installed:

* A C compiler (e.g., **GCC**)
* **make** build tool

---

## **Building the Engine**

The project includes a Makefile that supports both optimized and debug builds.

### **Release Build (Optimized)**

```bash
make
```

### **Debug Build (Unoptimized + Debug Symbols)**

```bash
make DEBUG=1
```

### **Output Location**

After building, the engine executable will appear in:

```txt
build/chess_engine
```

---

## **Making Moves**

Type moves in either notation, interchangeably, move to move:

| Notation | Example | Promotion |
| --- | --- | --- |
| Long algebraic | `e2e4` | append the piece letter: `a7a8q` |
| Standard Algebraic (SAN) | `e4`, `Nf3`, `O-O` | append `=`: `e8=Q` |

Promotion defaults to queen if you don't specify one, in either notation.

## **Commands**

The ones you'll actually use:

| Command | Does |
| --- | --- |
| `save` | Save the position to `board.txt`, resuming automatically next time you run the engine |
| `undo` | Take back your last move and the engine's reply |
| `resign` | Resign (engine wins) |
| `draw` | Offer a draw (the engine accepts unless it's clearly ahead) |
| `quit` | Exit |

The rest are there when you need them — inspecting/editing the position, tuning the AI, or reviewing a game:

| Command | Does |
| --- | --- |
| `fen` | Print the current position as a FEN string |
| `loadfen` | Load a position from a FEN string you paste in |
| `moves` | Print the game's move list in SAN |
| `pgn` | Export the game so far to `game.pgn` (also happens automatically when the game ends) |
| `depth` | View/change the engine's search depth — higher is stronger but slower |
| `time` | View/change the engine's per-move time budget in seconds (`0` = no cap) |

A game also ends automatically on checkmate, stalemate, the 50-move rule, insufficient material, or threefold repetition — no command needed. The board renders with Unicode chess glyphs, colored automatically in a real terminal (set `NO_COLOR` to turn that off).

---

## **Command-Line Options**

All optional; combine freely.

| Flag | Effect | Example |
| --- | --- | --- |
| `--fen "<fen>"` | Start from a given position instead of the standard opening (or a saved `board.txt`) | `--fen "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"` |
| `--side <white\|black>` | Choose which color you play locally (default `white`); the engine takes the other one and moves first if you pick `black` | `--side black` |
| `--depth <n>` | Set the engine's search depth (default 6) | `--depth 4` |
| `--time <seconds>` | Set the engine's per-move time budget (default 5s, `0` disables it) | `--time 3` |
| `--clock <min>+<inc>` | Play under a real Fischer clock instead of a flat time budget — see below | `--clock 5+3` |
| `--lichess <gameId>` | Play a live Lichess game — needs `make LICHESS=1`, see below | `--lichess abcd1234` |
| `--bot` | With `--lichess`: have the engine play its own moves | `--lichess abcd1234 --bot` |
| `--uci` | Speak UCI on stdin/stdout instead of starting the REPL — see below | `--uci` |

## **Playing With a Real Chess Clock**

`--clock <minutes>+<increment>` (e.g. `--clock 5+3` for 5 minutes with a 3-second increment, or `--clock 10` for no increment) gives both sides a real Fischer clock instead of the flat `--depth`/`--time` budget — running out of time loses the game. Under a clock, the engine decides how long to think each move from how much time is actually left, the increment, and the game phase, the same kind of time management a real chess engine uses. Details: [docs/ONLINE_PLAY.md](docs/ONLINE_PLAY.md).

---

## **Playing on Lichess**

The engine can play a live game on [lichess.org](https://lichess.org) from the terminal via Lichess's [Board API](https://lichess.org/api#tag/Board) (no chess.com support — it has no live-play API to talk to). This is the one feature with an external dependency ([libcurl](https://curl.se/libcurl/)), so it's off by default.

1. **Build with it enabled:** `make LICHESS=1`
2. **Get a personal API token** at [lichess.org/account/oauth/token](https://lichess.org/account/oauth/token) with the **"Play games with the board API" (`board:play`)** scope checked — without it, reading account info and watching a game both work fine, but *sending a move fails*, since that specifically needs `board:play`. Then export it: `export LICHESS_API_TOKEN=lip_xxxxxxxxxxxx`
3. **Create or accept a game** on lichess.org, then: `./build/chess_engine --lichess <gameId>`

Moves you type are relayed to Lichess and the opponent's moves stream back automatically. Your color is whatever Lichess assigned you for that game (detected automatically from the game data) — `--side` has no effect here, since it's only meaningful for local play, where there's no server to assign a side.

**Board API time control limits**: only Rapid, Classical, and Correspondence games work; Blitz also works but only for direct challenges, games vs the Lichess AI, or bulk pairing. **Bullet and UltraBullet are never supported by the Board API**, regardless of how the game was created — Lichess rejects the connection outright (HTTP 400).

**`--bot` mode is different, and needs its own setup.** The Board API explicitly forbids engine assistance (it's meant for relaying a *human's* moves from a physical board) — automatic play is only allowed through Lichess's separate **Bot API**, which `--bot` switches to entirely:

```bash
./build/chess_engine --lichess <gameId> --bot
```

This needs a **dedicated Bot account** (never your main one): generate a token with the **`bot:play`** scope instead, then, on an account that has **never played a single game** (this is required, and the upgrade is **irreversible** — a Bot account can never play rated games again or revert to normal):

```bash
curl -d '' https://lichess.org/api/bot/account/upgrade -H "Authorization: Bearer <bot-account-token>"
```

The Bot API allows all time controls except UltraBullet — bullet included. `./build/chess_engine` checks this itself before doing anything else and refuses to run `--bot` against a non-Bot account, rather than attempting the upgrade for you.

**If something fails**, the engine prints the actual HTTP status and Lichess's error message (e.g. `{"error":"Missing scope"}`), plus a hint for the common cases (401 = bad/expired token, 403 = missing scope, 400 on stream open = usually the Board API time-control restriction above). Details: [docs/ONLINE_PLAY.md](docs/ONLINE_PLAY.md).

---

## **Using a UCI GUI**

`--uci` puts the engine in [UCI protocol](https://en.wikipedia.org/wiki/Universal_Chess_Interface) mode — reading commands from stdin and replying on stdout — instead of starting the interactive REPL, so it can be driven by a standard chess GUI or match runner (e.g. [cutechess](https://github.com/cutechess/cutechess)) instead of a human typing moves. No build flag needed; it's always compiled in.

In cutechess, add an engine with `build/chess_engine` as the command and `--uci` as its argument. For `cutechess-cli`:

```bash
cutechess-cli -engine cmd=build/chess_engine arg=--uci name=C-ChessEngine \
              -engine cmd=/path/to/other/engine name=Other \
              -each proto=uci tc=40/60 -rounds 10
```

Time controls (`wtime`/`btime`/`winc`/`binc`, `movetime`, or a fixed `depth`) are all supported — see [docs/UCI.md](docs/UCI.md) for exactly which commands are handled and the one real limitation (no true "stop" mid-search, since the search is a single blocking call rather than a background thread — this doesn't affect normal timed play, only pondering/infinite-analysis mode).

---

## **Project Architecture**

Full write-up (module diagram, data flow, key invariants): [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Headers live in `include/`, implementation in `src/`.

| File                    | Role              | Description                                                                   | Docs |
| ----------------------- | ----------------- | ----------------------------------------------------------------------------- | ---- |
| **main.c**              | Entry Point & UI  | Handles board display, input parsing, and the game loop.                      | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **structs.h**           | Data Structures   | Defines all core types such as `Piece`, `Move`, `MoveList`, and `BoardState`. | [BOARD_AND_RULES.md](docs/BOARD_AND_RULES.md) |
| **game.c / game.h**     | Game Logic        | Implements `makeMove`, `undoMove`, attack detection, and rule enforcement.    | [BOARD_AND_RULES.md](docs/BOARD_AND_RULES.md) |
| **ai.c / ai.h**         | Search Engine     | Contains NegaMax, Alpha-Beta, Quiescence Search, and move generation.         | [SEARCH_AND_EVAL.md](docs/SEARCH_AND_EVAL.md) |
| **eval.c / eval.h**     | Evaluation System | Implements material scoring, PSTs, and tapered MG/EG evaluation.              | [SEARCH_AND_EVAL.md](docs/SEARCH_AND_EVAL.md) |
| **fileio.c / fileio.h** | Persistence Layer | Loads and saves a simplified custom text representation (`board.txt`).       | [NOTATION_AND_FORMATS.md](docs/NOTATION_AND_FORMATS.md) |
| **notation.c / notation.h** | Notation      | SAN parsing/printing, real FEN import/export, PGN export, long algebraic.     | [NOTATION_AND_FORMATS.md](docs/NOTATION_AND_FORMATS.md) |
| **timecontrol.c / timecontrol.h** | Chess Clock | Fischer clock bookkeeping (remaining time, increment, flag-fall) shared by local and Lichess play. | [ONLINE_PLAY.md](docs/ONLINE_PLAY.md) |
| **lichess.c / lichess.h** (optional) | Online Play | Board API client for playing a live Lichess game (optionally as a bot) from the terminal. | [ONLINE_PLAY.md](docs/ONLINE_PLAY.md) |
| **uci.c / uci.h**       | UCI Front End     | Minimal UCI protocol loop, for driving the engine from cutechess/Arena/etc.   | [UCI.md](docs/UCI.md) |

---

## **Running Tests**

The project has a lightweight, dependency-free unit test suite covering every module (move generation, `makeMove`/`undoMove`, evaluation, save/load, notation, and the search) except `lichess.c`, which needs a live network connection and account. See [docs/TESTING.md](docs/TESTING.md) for what's covered, what isn't, and how to add a new test.

```bash
make test
```

This builds `build/run_tests` and runs it, printing a pass/fail summary. It includes a [perft](https://www.chessprogramming.org/Perft) check (`perft(3) == 8902` from the starting position), which is a strong end-to-end regression test for the legal move generator.

---

## **Generated API Documentation**

Every header in `include/` carries Doxygen-style docstrings. If you have [Doxygen](https://www.doxygen.nl/) installed:

```bash
make docs
```

generates browsable HTML into `build/docs/html/` (open `index.html`). This is separate from — and generated from — the hand-written docs in [docs/](docs/README.md); it's a reference for function-level detail, while `docs/` explains the *why* and how things fit together.

---

## **Cleaning Up**

Remove build artifacts:

```bash
make clean
```

Remove build artifacts and the saved board state:

```bash
make distclean
```

---
