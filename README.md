# **C-ChessEngine**

A fast, console-based chess engine implemented entirely in C. This project demonstrates a strong understanding of game state management, move generation, and search algorithms, including **NegaMax with Alpha-Beta Pruning** and a **Tapered Evaluation** function.

## **Key Features**

* **Console Interface:** Simple command-line input/output for interacting with the game.  
* **UCI-Ready Architecture (Conceptual):** The underlying move and board state logic is well-structured, making it suitable for future integration with the Universal Chess Interface (UCI).  
* **Advanced AI Search:** Utilizes the **NegaMax** algorithm with **Alpha-Beta Pruning** for efficient tree searching.  
* **Quiescence Search:** Implemented to mitigate the **Horizon Effect** by only evaluating stable positions after tactical exchanges (captures).  
* **Move Ordering:** Employs **MVV-LVA (Most Valuable Victim \- Least Valuable Aggressor)** to prioritize critical capture moves, significantly improving Alpha-Beta pruning efficiency.  
* **Tapered Evaluation:** The evaluation function dynamically switches between a **Middlegame (MG)** score (prioritizing King safety and development) and an **Endgame (EG)** score (prioritizing King activity and Pawn promotion) based on the remaining material.  
* **Game Persistence:** Ability to **save and load** the current game state (board.txt).

## **Getting Started**

### **Prerequisites**

You need a C compiler (like GCC) and the make utility installed on your system.

### **Building the Engine**

The project includes a robust Makefile for compilation.

1. **Clone the repository (if applicable) or ensure all files are in the same directory.**  
2. **Build the Release Version (Optimized):**  
   make

3. **Build the Debug Version (with symbols and no optimization):**  
   make DEBUG=1

This will create an executable file named chess\_engine inside the newly created build/ directory.

### **Running the Game**

To start a game immediately after building:  
make run

Alternatively, run the executable directly:  
./build/chess\_engine

### **Game Commands**

The engine defaults to a standard starting position if no board.txt file is found. You play as White, and the AI plays as Black.

| Command | Description | Example |
| :---- | :---- | :---- |
| **Move** | Enter a move using standard algebraic notation (start square followed by end square). | e2e4 |
| **Promotion** | Append the piece type (q, r, b, n) for pawn promotions. Queen is the default if omitted. | a7a8q |
| save | Saves the current board state to board.txt. | save |
| quit | Exits the game. | quit |

## **Project Architecture**

The engine is modularized into several C files, each responsible for a specific aspect of the chess logic:

| File | Role | Description |
| :---- | :---- | :---- |
| main.c | **Entry Point & UI** | Handles command-line I/O, board printing, move parsing (e2e4), and the main game loop. |
| structs.h | **Data Definitions** | Defines core structures like Piece, Position, Move, MoveList, and the overall BoardState. |
| game.c / game.h | **Game Logic** | Manages the board state. Contains functions for **makeMove**, **undoMove** (essential for search), and checking for check/attacks (isKingInCheck, isSquareAttacked). |
| ai.c / ai.h | **Artificial Intelligence** | Implements the core search algorithm (**NegaMax with Alpha-Beta Pruning**), **Quiescence Search**, and move generation (generateAllLegalMoves). |
| eval.c / eval.h | **Evaluation Function** | Calculates the static score of a position using **Tapered Evaluation**. Includes Material tables and comprehensive Piece-Square Tables (PSTs) that change value between the middlegame and endgame. |
| fileio.c / fileio.h | **Persistence** | Handles loading and saving the game state to/from a text file (board.txt), based on a simplified FEN-like structure. |

## **Cleaning Up**

To remove all compiled files (build/ directory):  
make clean

To remove compiled files and the save file (board.txt):  
make distclean  
