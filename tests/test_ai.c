#include <stdio.h>
#include <stdbool.h>
#include <time.h>

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

static void test_search_depth_is_adjustable(void)
{
    SECTION("setSearchDepth / getSearchDepth");

    int original = getSearchDepth();

    setSearchDepth(3);
    CHECK(getSearchDepth() == 3, "setSearchDepth updates the depth used by findBestMove");

    setSearchDepth(0);
    CHECK(getSearchDepth() == 3, "setSearchDepth ignores non-positive values");

    setSearchDepth(original);
}

static void test_search_time_limit_is_adjustable(void)
{
    SECTION("setSearchTimeLimit / getSearchTimeLimit");

    double original = getSearchTimeLimit();

    setSearchTimeLimit(2.5);
    CHECK(getSearchTimeLimit() == 2.5, "setSearchTimeLimit updates the configured time cap");

    setSearchTimeLimit(original);
}

static void test_time_cap_interrupts_a_deep_search(void)
{
    SECTION("a tight time cap interrupts a deep search and still returns a legal move");

    int originalDepth = getSearchDepth();
    double originalTimeLimit = getSearchTimeLimit();

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    setSearchDepth(20);      // deep enough that an uncapped search would take a very long time
    setSearchTimeLimit(0.1); // 100ms cap

    clock_t start = clock();
    Move best = findBestMove(&board);
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;

    setSearchDepth(originalDepth);
    setSearchTimeLimit(originalTimeLimit);

    CHECK(elapsed < 2.0, "the search returns well within a couple of seconds despite depth 20");

    MoveList legalMoves = generateAllLegalMoves(&board);
    bool isLegal = false;
    for (int i = 0; i < legalMoves.count; i++)
    {
        Move m = legalMoves.moves[i];
        if (m.from.row == best.from.row && m.from.col == best.from.col &&
            m.to.row == best.to.row && m.to.col == best.to.col)
        {
            isLegal = true;
            break;
        }
    }
    CHECK(isLegal, "the move returned under time pressure is still a fully legal move");
}

static void test_compute_move_time_budget(void)
{
    SECTION("computeMoveTimeBudget");

    BoardState startPos;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &startPos);

    // More remaining time should never shrink the budget.
    double budgetLow = computeMoveTimeBudget(&startPos, 30.0, 0.0);
    double budgetHigh = computeMoveTimeBudget(&startPos, 300.0, 0.0);
    CHECK(budgetHigh > budgetLow, "more remaining time yields a larger move budget");
    CHECK(budgetHigh < 300.0, "the budget never exceeds the remaining time");
    CHECK(budgetLow > 0.0, "the budget is always positive");

    // A larger increment should add to the budget, not subtract from it.
    double budgetNoInc = computeMoveTimeBudget(&startPos, 60.0, 0.0);
    double budgetWithInc = computeMoveTimeBudget(&startPos, 60.0, 5.0);
    CHECK(budgetWithInc > budgetNoInc, "a larger increment yields a larger move budget");

    // Panic mode: critically low time should spend only a sliver of it.
    double budgetCritical = computeMoveTimeBudget(&startPos, 2.0, 0.0);
    CHECK(budgetCritical < 2.0, "the panic-mode budget never exceeds the time left");
    CHECK(budgetCritical < 1.0, "the panic-mode budget is a small sliver of a critical clock");

    // Phase awareness: a position with meaningful material left (a rough
    // middlegame, phase 8) should get more time than a near-bare endgame
    // (phase 0) with an identical clock.
    BoardState middlegame;
    fenToBoard("4k2r/8/5n2/2b5/5B2/2N5/8/R3K3 w - - 0 1", &middlegame);
    BoardState bareEndgame;
    fenToBoard("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", &bareEndgame);

    double middlegameBudget = computeMoveTimeBudget(&middlegame, 120.0, 0.0);
    double endgameBudget = computeMoveTimeBudget(&bareEndgame, 120.0, 0.0);
    CHECK(middlegameBudget > endgameBudget,
          "a position with more material left gets more time than a near-bare endgame on the same clock");
}

static void test_find_best_move_timed_respects_the_clock(void)
{
    SECTION("findBestMoveTimed returns quickly and legally under a tight clock");

    int originalDepth = getSearchDepth();
    setSearchDepth(20); // deep enough that an uncapped search would take a very long time
    double limitBeforeCall = getSearchTimeLimit();

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    clock_t start = clock();
    Move best = findBestMoveTimed(&board, 3.0, 0.0); // 3 seconds left, no increment
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;

    setSearchDepth(originalDepth);

    CHECK(elapsed < 3.0, "findBestMoveTimed doesn't overrun the time actually left on the clock");
    CHECK(getSearchTimeLimit() == limitBeforeCall,
          "findBestMoveTimed restores the previous global time limit afterward");

    MoveList legalMoves = generateAllLegalMoves(&board);
    bool isLegal = false;
    for (int i = 0; i < legalMoves.count; i++)
    {
        Move m = legalMoves.moves[i];
        if (m.from.row == best.from.row && m.from.col == best.from.col &&
            m.to.row == best.to.row && m.to.col == best.to.col)
        {
            isLegal = true;
            break;
        }
    }
    CHECK(isLegal, "findBestMoveTimed still returns a fully legal move under time pressure");
}

void run_ai_tests(void)
{
    test_finds_mate_in_one();
    test_search_depth_is_adjustable();
    test_search_time_limit_is_adjustable();
    test_time_cap_interrupts_a_deep_search();
    test_compute_move_time_budget();
    test_find_best_move_timed_respects_the_clock();
}
