#ifndef STRUCTS_H
#define STRUCTS_H

#define MAX_MOVES_IN_LIST 512

// --- Piece / Color ---

typedef enum {
    EMPTY,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING
} PieceType;

typedef enum {
    WHITE,
    BLACK,
    NO_COLOR
} PieceColor;

// --- Basic Types ---

typedef struct {
    int row;
    int col;
} Position;

typedef struct {
    PieceType type;
    PieceColor color;
} Piece;

// --- Move Flags ---

#define MOVE_NORMAL 0
#define MOVE_PROMOTION 1
#define MOVE_EN_PASSANT 2
#define MOVE_CASTLE_KING 3
#define MOVE_CASTLE_QUEEN 4

// --- Move Struct ---

typedef struct {
    Position from;
    Position to;
    PieceType promotion; // For promotion moves; otherwise EMPTY
    int flag;            // MOVE_* constants
} Move;

// --- Move List ---

typedef struct {
    Move moves[MAX_MOVES_IN_LIST];
    int count;
} MoveList;

// --- Castling Rights ---

typedef struct {
    int wk; // white king-side
    int wq; // white queen-side
    int bk; // black king-side
    int bq; // black queen-side
} CastlingRights;

// --- Board State ---

typedef struct {
    Piece squares[8][8];       // Board representation
    PieceColor currentPlayer;  // Side to move

    CastlingRights castling;   // Current castling rights

    Position enPassantTarget;  // Square behind pawn that moved 2 squares (or -1,-1)

    int halfmoveClock;         // For 50-move rule
    int fullmoveNumber;        // Counts moves starting from 1
} BoardState;

#endif
