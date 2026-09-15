/**
 * @file structs.h
 * @brief Core data types shared by every module in the engine: pieces,
 * squares, moves, and the board itself.
 *
 * See docs/BOARD_AND_RULES.md for the full write-up of the coordinate
 * system and board representation this file defines.
 */

#ifndef STRUCTS_H
#define STRUCTS_H

/** Maximum number of moves a single MoveList can hold. 512 is comfortably
 * above the maximum number of legal moves possible in any real position
 * (~218), with headroom for the pseudo-legal lists generated before the
 * king-safety filter in ai.c's generateAllLegalMoves(). */
#define MAX_MOVES_IN_LIST 512

// --- Piece / Color ---

/** The kind of piece on a square, or EMPTY for no piece. */
typedef enum
{
    EMPTY,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING
} PieceType;

/** Which side a piece belongs to. NO_COLOR marks an empty square. */
typedef enum
{
    WHITE,
    BLACK,
    NO_COLOR
} PieceColor;

// --- Basic Types ---

/**
 * @brief A square on the board, addressed by array indices rather than
 * chess notation.
 *
 * @note Coordinate convention (important, and used everywhere in this
 * codebase): row 0 is rank 8 and row 7 is rank 1 - the board array is
 * stored "as printed" from Black's back rank down to White's. Converting
 * to/from algebraic notation is always `row = 8 - rank` and
 * `col = file - 'a'`. A square with row/col of -1 (e.g. Position{-1,-1})
 * is used as the "no such square" sentinel, e.g. for enPassantTarget when
 * no en passant capture is currently possible.
 */
typedef struct
{
    int row;
    int col;
} Position;

/** A piece occupying (or, with type EMPTY, not occupying) a square. */
typedef struct
{
    PieceType type;
    PieceColor color;
} Piece;

// --- Move Flags ---

#define MOVE_NORMAL 0       ///< An ordinary move (including captures)
#define MOVE_PROMOTION 1    ///< A pawn move onto the back rank; `promotion` holds the new piece
#define MOVE_EN_PASSANT 2   ///< A pawn capturing another pawn en passant
#define MOVE_CASTLE_KING 3  ///< King-side castling (O-O)
#define MOVE_CASTLE_QUEEN 4 ///< Queen-side castling (O-O-O)

// --- Move Struct ---

/**
 * @brief A single chess move, as produced by move generation (ai.c) and
 * consumed by makeMove()/undoMove() (game.c).
 *
 * A Move only fully describes the move once its `flag` has been resolved
 * against the current position - notation.c's resolveMove() does this for
 * moves typed by a human, since e.g. "e1g1" alone doesn't say whether it's
 * a normal king move or castling.
 */
typedef struct
{
    Position from;
    Position to;
    PieceType promotion; ///< The piece promoted to; EMPTY unless flag == MOVE_PROMOTION
    int flag;            ///< One of the MOVE_* constants above
} Move;

// --- Move List ---

/** A fixed-capacity list of moves, filled in by ai.c's move generators. */
typedef struct
{
    Move moves[MAX_MOVES_IN_LIST];
    int count;
} MoveList;

// --- Castling Rights ---

/** Whether each side still has the *right* to castle on each side - this
 * says nothing about whether castling is legal right now (blocked pieces,
 * king in check, etc. are checked separately); it only tracks whether the
 * king and that rook have never yet moved (or, for the opponent's rook,
 * been captured on its home square). Each field is 1 (still available) or
 * 0 (permanently forfeited). */
typedef struct
{
    int wk; ///< White king-side (O-O)
    int wq; ///< White queen-side (O-O-O)
    int bk; ///< Black king-side (O-O)
    int bq; ///< Black queen-side (O-O-O)
} CastlingRights;

// --- Board State ---

/**
 * @brief The complete state of a chess position - everything needed to
 * generate legal moves, detect draws, and serialize to/from FEN.
 *
 * This is the one struct nearly every module operates on: game.c mutates
 * it via makeMove()/undoMove(), ai.c reads it to generate moves and search,
 * eval.c reads it to score a position, and notation.c converts it to and
 * from FEN/SAN/PGN text.
 */
typedef struct
{
    Piece squares[8][8];      ///< The board; see Position's doc comment for the row/col convention
    PieceColor currentPlayer; ///< Side to move

    CastlingRights castling; ///< Current castling rights for both sides

    Position enPassantTarget; ///< The square a pawn can capture onto en passant, or {-1,-1} if none

    int halfmoveClock;  ///< Half-moves since the last capture or pawn move (FIDE 50-move rule)
    int fullmoveNumber; ///< Starts at 1, incremented after each Black move
} BoardState;

#endif
