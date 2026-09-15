#include <stdio.h>

#include "structs.h"
#include "ai.h"
#include "game.h"
#include "notation.h"
#include "test_common.h"

static void test_finds_mate_in_one(void)
{
    SECTION("findBestMove finds a forced mate in one");

    /* Rd1-d8 is checkmate against the black king boxed in by its own pawns. */
    BoardState board;
    fenToBoard("7k/5ppp/8/8/8/8/8/3RK3 w - - 0 1", &board);

    Move best = findBestMove(&board);
    CHECK(best.from.row == 7 && best.from.col == 3, "findBestMove picks up the rook on d1");
    CHECK(best.to.row == 0 && best.to.col == 3, "findBestMove delivers Rd8#");

    makeMove(&board, best);
    MoveList replies = generateAllLegalMoves(&board);
    CHECK(isKingInCheck(&board, BLACK) && replies.count == 0, "the move played is indeed checkmate");
}

void run_ai_tests(void)
{
    test_finds_mate_in_one();
}
