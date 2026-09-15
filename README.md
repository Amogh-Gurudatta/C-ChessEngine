# **C-ChessEngine**

[![CI](https://github.com/Amogh-Gurudatta/C-ChessEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/Amogh-Gurudatta/C-ChessEngine/actions/workflows/ci.yml)

A high-performance, console-based chess engine written entirely in C. This project showcases efficient board representation, legal move generation, and advanced search algorithms including **NegaMax with Alpha-Beta Pruning**, **Quiescence Search**, and **Tapered Evaluation**.

---

## **Key Features**

* **Console-Based Interface:** Simple text-based input for playing moves and executing commands.
* **Modular Architecture:** Clean separation of game logic, move generation, search, evaluation, and file I/O—making the engine suitable for future UCI integration.
* **Advanced Search Algorithms:**

  * **NegaMax + Alpha-Beta Pruning** for efficient game‑tree search
  * **Iterative Deepening** with an optional time cap, so the engine can stop and return its best move so far instead of searching indefinitely
  * **Quiescence Search** to reduce the horizon effect
  * **MVV-LVA move ordering** to improve pruning efficiency
* **Adjustable Difficulty:** Search depth and/or a per-move time budget can be set from the command line or mid-game.
* **Real Chess Clocks:** Fischer-style clocks (time + increment) for both sides, with the engine managing its own thinking time based on time left, increment, and game phase — like a real chess engine, not a flat per-move cap.
* **Tapered Evaluation:** Blends **Middlegame (MG)** and **Endgame (EG)** heuristics dynamically based on remaining material.
* **Game Persistence:** Save and load game states through a simple `board.txt` file.
* **Full Draw Detection:** Checkmate, stalemate, the 50-move rule, insufficient material, and threefold repetition are all detected and end the game automatically.
* **Standard Notation:** Accepts and prints Standard Algebraic Notation (`e4`, `Nf3`, `O-O`), and can import/export real FEN and PGN.
* **Lichess Board API (optional):** Play a live Lichess game from the terminal (see [Playing on Lichess](#playing-on-lichess)).

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

## **Running the Engine**

To build and run in one step:

```bash
make run
```

Or execute the compiled binary directly:

```bash
./build/chess_engine
```

---

## **Gameplay & Commands**

If `board.txt` is not found, the engine loads the standard chess starting position.
You play as **White**, and the engine plays as **Black**.

| Command       | Description                                                  | Example        |
| ------------- | ------------------------------------------------------------- | -------------- |
| **Move**      | Play a move in long algebraic notation                        | `e2e4`         |
| **Move (SAN)**| Or play a move in Standard Algebraic Notation                 | `e4`, `Nf3`, `O-O` |
| **Promotion** | Append `q`, `r`, `b`, or `n` (long algebraic); `=Q` etc. (SAN); defaults to queen | `a7a8q`, `e8=Q` |
| **save**      | Save the current position to `board.txt`                      | `save`         |
| **fen**       | Print the current position as a standard FEN string           | `fen`          |
| **loadfen**   | Load a position from a FEN string you paste in                | `loadfen`      |
| **moves**     | Print the game's move list in SAN                              | `moves`        |
| **pgn**       | Export the game so far to `game.pgn`                           | `pgn`          |
| **undo**      | Take back your last move (and the engine's reply)              | `undo`         |
| **depth**     | View or change the engine's search depth (higher = stronger, slower) | `depth`  |
| **time**      | View or change the engine's per-move time cap in seconds (0 disables it) | `time` |
| **resign**    | Resign the game (Black/AI wins)                                | `resign`       |
| **draw**      | Offer a draw; the engine accepts unless it's clearly ahead     | `draw`         |
| **quit**      | Exit the engine                                                | `quit`         |

A finished game is automatically exported to `game.pgn`. The game ends automatically on checkmate, stalemate, the 50-move rule, insufficient material, or threefold repetition.

You can also start the engine directly from a FEN string, with a custom search depth (default 6 plies), and/or with a per-move time cap in seconds (default 5s; the engine searches iteratively deeper and returns its best move so far once the cap is hit):

```bash
./build/chess_engine --fen "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" --depth 4 --time 3
```

The board is drawn with Unicode chess glyphs, colored automatically when stdout is a real terminal. Set the `NO_COLOR` environment variable to disable the coloring (colors are always off when output is redirected/piped).

### **Playing with a real chess clock**

Add `--clock <minutes>+<increment>` to play under a real Fischer clock — both sides start with the same time, and gain the increment after each move they complete. Running out of time loses the game (a move that arrives after your flag has fallen doesn't count, just like a real clock).

```bash
./build/chess_engine --clock 5+3   # 5 minutes per side, +3 seconds per move
./build/chess_engine --clock 10    # 10 minutes per side, no increment
```

With a clock running, the engine no longer uses `--depth`/`--time` as a flat cap — instead it decides how long to think on each move from how much time is actually left, the increment, and the game phase (spending more in a complex middlegame, less in a simplified endgame, and going into "panic mode" once critically low), the same kind of time management a real chess engine uses.

---

## **Playing on Lichess**

The engine can play a live game on [lichess.org](https://lichess.org) from the terminal, using Lichess's [Board API](https://lichess.org/api#tag/Board). This talks to a real Lichess game over HTTP — there's no support for chess.com, since it doesn't offer a live-play API.

This feature is optional and off by default, since it's the only part of the project with an external dependency ([libcurl](https://curl.se/libcurl/)).

**1. Build with Lichess support:**

```bash
make LICHESS=1
```

**2. Create a personal API token** at [lichess.org/account/oauth/token](https://lichess.org/account/oauth/token) (with board play permission) and export it:

```bash
export LICHESS_API_TOKEN=lip_xxxxxxxxxxxx
```

**3. Create or accept a game on lichess.org**, then run the engine with that game's ID:

```bash
./build/chess_engine --lichess <gameId>
```

Moves you type (long algebraic or SAN) are sent to Lichess; your opponent's moves are streamed back and applied automatically.

**Bot mode:** add `--bot` to have the engine play its own moves automatically instead of prompting you, using the real time control (`wtime`/`btime`/`winc`/`binc`) Lichess reports for the game — the same clock-aware time management as `--clock` for local play, but driven by the actual match clock instead of a locally-configured one:

```bash
./build/chess_engine --lichess <gameId> --bot
```

---

## **Project Architecture**

| File                    | Role              | Description                                                                   |
| ----------------------- | ----------------- | ----------------------------------------------------------------------------- |
| **main.c**              | Entry Point & UI  | Handles board display, input parsing, and the game loop.                      |
| **structs.h**           | Data Structures   | Defines all core types such as `Piece`, `Move`, `MoveList`, and `BoardState`. |
| **game.c / game.h**     | Game Logic        | Implements `makeMove`, `undoMove`, attack detection, and rule enforcement.    |
| **ai.c / ai.h**         | Search Engine     | Contains NegaMax, Alpha-Beta, Quiescence Search, and move generation.         |
| **eval.c / eval.h**     | Evaluation System | Implements material scoring, PSTs, and tapered MG/EG evaluation.              |
| **fileio.c / fileio.h** | Persistence Layer | Loads and saves a simplified custom text representation (`board.txt`).       |
| **notation.c / notation.h** | Notation      | SAN parsing/printing, real FEN import/export, PGN export, long algebraic.     |
| **timecontrol.c / timecontrol.h** | Chess Clock | Fischer clock bookkeeping (remaining time, increment, flag-fall) shared by local and Lichess play. |
| **lichess.c / lichess.h** (optional) | Online Play | Board API client for playing a live Lichess game (optionally as a bot) from the terminal. |

---

## **Running Tests**

The project has a lightweight, dependency-free unit test suite covering every module (move generation, `makeMove`/`undoMove`, evaluation, save/load, notation, and the search) except `lichess.c`, which needs a live network connection and account.

```bash
make test
```

This builds `build/run_tests` and runs it, printing a pass/fail summary. It includes a [perft](https://www.chessprogramming.org/Perft) check (`perft(3) == 8902` from the starting position), which is a strong end-to-end regression test for the legal move generator.

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
