# **C-ChessEngine**

A high-performance, console-based chess engine written entirely in C. This project showcases efficient board representation, legal move generation, and advanced search algorithms including **NegaMax with Alpha-Beta Pruning**, **Quiescence Search**, and **Tapered Evaluation**.

---

## **Key Features**

* **Console-Based Interface:** Simple text-based input for playing moves and executing commands.
* **Modular Architecture:** Clean separation of game logic, move generation, search, evaluation, and file I/O—making the engine suitable for future UCI integration.
* **Advanced Search Algorithms:**

  * **NegaMax + Alpha-Beta Pruning** for efficient game‑tree search
  * **Quiescence Search** to reduce the horizon effect
  * **MVV-LVA move ordering** to improve pruning efficiency
* **Tapered Evaluation:** Blends **Middlegame (MG)** and **Endgame (EG)** heuristics dynamically based on remaining material.
* **Game Persistence:** Save and load game states through a simple `board.txt` file.
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
| **quit**      | Exit the engine                                                | `quit`         |

A finished game is automatically exported to `game.pgn`.

You can also start the engine directly from a FEN string:

```bash
./build/chess_engine --fen "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
```

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
| **lichess.c / lichess.h** (optional) | Online Play | Board API client for playing a live Lichess game from the terminal.          |

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
