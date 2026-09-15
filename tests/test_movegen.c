#include <stdio.h>
#include <stdbool.h>

#include "structs.h"
#include "ai.h"
#include "game.h"
#include "notation.h"
#include "test_common.h"

/* Standard perft: counts leaf nodes of the legal move tree at a given
 * depth. A strong regression check for move generation + make/undo,
 * since it's sensitive to any missing/illegal/duplicated move. */
static long perft(BoardState *board, int depth)
{
    if (depth == 0)
        return 1;

    MoveList moves = generateAllLegalMoves(board);
    if (depth == 1)
        return moves.count;

    long nodes = 0;
    for (int i = 0; i < moves.count; i++)
    {
        makeMove(board, moves.moves[i]);
        nodes += perft(board, depth - 1);
        undoMove(board, moves.moves[i]);
    }
    return nodes;
}

static void test_perft_start_position(void)
{
    SECTION("perft on the standard starting position");

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    CHECK(perft(&board, 1) == 20, "perft(1) == 20");
    CHECK(perft(&board, 2) == 400, "perft(2) == 400");
    CHECK(perft(&board, 3) == 8902, "perft(3) == 8902");
}

static void test_pinned_piece_cannot_move(void)
{
    SECTION("a pinned piece is excluded from legal moves");

    /* White rook on e2 is pinned to its king by the black rook on e8;
     * it may only move along the e-file, not sideways. */
    BoardState board;
    fenToBoard("4r3/8/8/8/8/8/4R3/4K3 w - - 0 1", &board);

    MoveList moves = generateAllLegalMoves(&board);

    bool sidewaysMoveFound = false;
    bool alongFileMoveFound = false;
    for (int i = 0; i < moves.count; i++)
    {
        Move m = moves.moves[i];
        if (m.from.row == 6 && m.from.col == 4)
        {
            if (m.to.col != 4)
                sidewaysMoveFound = true;
            else
                alongFileMoveFound = true;
        }
    }
    CHECK(!sidewaysMoveFound, "the pinned rook cannot move off the pin line");
    CHECK(alongFileMoveFound, "the pinned rook can still move along the pin line");
}

static void test_checkmate_has_no_legal_moves(void)
{
    SECTION("a checkmate position has zero legal moves");

    /* Black, boxed in by its own pawns, is mated by the rook on d8. */
    BoardState board;
    fenToBoard("3R3k/5ppp/8/8/8/8/8/4K3 b - - 1 1", &board);

    CHECK(isKingInCheck(&board, BLACK), "the black king is in check from the rook on d8");
    MoveList moves = generateAllLegalMoves(&board);
    CHECK(moves.count == 0, "the mated side has zero legal moves");
}

static void test_stalemate_has_no_legal_moves(void)
{
    SECTION("a stalemate position has zero legal moves and no check");

    /* Classic stalemate: king boxed into a8 by the queen, with no legal move
     * and not itself attacked. */
    BoardState board;
    fenToBoard("k7/8/1Q6/8/8/8/8/4K3 b - - 0 1", &board);

    CHECK(!isKingInCheck(&board, BLACK), "the stalemated king is not in check");
    MoveList moves = generateAllLegalMoves(&board);
    CHECK(moves.count == 0, "the stalemated side has zero legal moves");
}

void run_movegen_tests(void)
{
    test_perft_start_position();
    test_pinned_piece_cannot_move();
    test_checkmate_has_no_legal_moves();
    test_stalemate_has_no_legal_moves();
}
