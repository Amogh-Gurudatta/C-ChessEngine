#include <stdio.h>

#include "structs.h"
#include "eval.h"
#include "notation.h"
#include "test_common.h"

static void test_start_position_is_balanced(void)
{
    SECTION("evaluateBoard on the start position");

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    CHECK(evaluateBoard(&board) == 0, "the symmetric starting position evaluates to exactly 0");
}

static void test_material_imbalance(void)
{
    SECTION("evaluateBoard reflects material imbalance");

    BoardState missingBlackQueen;
    fenToBoard("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &missingBlackQueen);
    CHECK(evaluateBoard(&missingBlackQueen) > 0, "missing a black queen favors White (positive score)");

    BoardState missingWhiteQueen;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1", &missingWhiteQueen);
    CHECK(evaluateBoard(&missingWhiteQueen) < 0, "missing a white queen favors Black (negative score)");
}

static void test_game_phase(void)
{
    SECTION("getGamePhase");

    BoardState startPos;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &startPos);
    CHECK(getGamePhase(&startPos) == 24, "the starting position is at full game phase (24)");

    BoardState bareKings;
    fenToBoard("4k3/8/8/8/8/8/8/4K3 w - - 0 1", &bareKings);
    CHECK(getGamePhase(&bareKings) == 0, "bare kings are at game phase 0");

    BoardState oneRookEach;
    fenToBoard("4k2r/8/8/8/8/8/8/R3K3 w - - 0 1", &oneRookEach);
    CHECK(getGamePhase(&oneRookEach) == 4, "one rook per side contributes 2 phase points each");
}

void run_eval_tests(void)
{
    test_start_position_is_balanced();
    test_material_imbalance();
    test_game_phase();
}
