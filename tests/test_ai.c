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

void run_ai_tests(void)
{
    test_finds_mate_in_one();
    test_search_depth_is_adjustable();
    test_search_time_limit_is_adjustable();
    test_time_cap_interrupts_a_deep_search();
}
