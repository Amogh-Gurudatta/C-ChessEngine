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

| Command       | Description                                     | Example |
| ------------- | ----------------------------------------------- | ------- |
| **Move**      | Play a move using long algebraic notation       | `e2e4`  |
| **Promotion** | Append `q`, `r`, `b`, or `n`; defaults to queen | `a7a8q` |
| **save**      | Save the current position to `board.txt`        | `save`  |
| **quit**      | Exit the engine                                 | `quit`  |

---

## **Project Architecture**

| File                    | Role              | Description                                                                   |
| ----------------------- | ----------------- | ----------------------------------------------------------------------------- |
| **main.c**              | Entry Point & UI  | Handles board display, input parsing, and the game loop.                      |
| **structs.h**           | Data Structures   | Defines all core types such as `Piece`, `Move`, `MoveList`, and `BoardState`. |
| **game.c / game.h**     | Game Logic        | Implements `makeMove`, `undoMove`, attack detection, and rule enforcement.    |
| **ai.c / ai.h**         | Search Engine     | Contains NegaMax, Alpha-Beta, Quiescence Search, and move generation.         |
| **eval.c / eval.h**     | Evaluation System | Implements material scoring, PSTs, and tapered MG/EG evaluation.              |
| **fileio.c / fileio.h** | Persistence Layer | Loads and saves a simplified FEN-like text representation.                    |

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

