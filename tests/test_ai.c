#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "structs.h"
#include "ai.h"
#include "book.h"
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

    // A correspondence game reports days per move as a clock: without an
    // absolute cap, the proportional formula turns that into hours of thinking.
    double threeDays = 3.0 * 86400.0;
    CHECK(computeMoveTimeBudget(&startPos, threeDays, 0.0) <= 30.0,
          "even a multi-day clock (correspondence) never yields more than the hard per-move cap");
    CHECK(computeMoveTimeBudget(&startPos, threeDays, 0.0) > 1.0,
          "the cap still leaves a real, useful thinking budget");
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

static bool movesAreEqual(Move a, Move b)
{
    return a.from.row == b.from.row && a.from.col == b.from.col &&
           a.to.row == b.to.row && a.to.col == b.to.col && a.promotion == b.promotion;
}

static void test_transposition_table_does_not_change_the_result(void)
{
    SECTION("the transposition table changes search speed, not the result");

    BoardState board;
    fenToBoard("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", &board);

    int originalDepth = getSearchDepth();
    double originalTimeLimit = getSearchTimeLimit();
    bool originalTTState = getUseTranspositionTable();
    bool originalBookState = getUseOpeningBook();

    setSearchDepth(4);
    setSearchTimeLimit(0); // no time cap: both runs must complete the full depth
    setUseOpeningBook(false); // this test wants an actual search, not a book move

    setUseTranspositionTable(false);
    Move moveWithoutTT = findBestMove(&board);

    setUseTranspositionTable(true);
    Move moveWithTT = findBestMove(&board);

    setSearchDepth(originalDepth);
    setSearchTimeLimit(originalTimeLimit);
    setUseTranspositionTable(originalTTState);
    setUseOpeningBook(originalBookState);

    CHECK(movesAreEqual(moveWithoutTT, moveWithTT),
          "enabling the transposition table does not change the move a full-depth search finds");
}

static void test_transposition_table_reduces_node_count(void)
{
    SECTION("the transposition table measurably reduces nodes searched");

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    int originalDepth = getSearchDepth();
    double originalTimeLimit = getSearchTimeLimit();
    bool originalTTState = getUseTranspositionTable();
    bool originalBookState = getUseOpeningBook();

    setSearchDepth(4);
    setSearchTimeLimit(0);
    setUseOpeningBook(false); // this test wants an actual search, not a book move

    setUseTranspositionTable(false);
    findBestMove(&board);
    long nodesWithoutTT = getLastSearchNodeCount();

    setUseTranspositionTable(true);
    findBestMove(&board);
    long nodesWithTT = getLastSearchNodeCount();

    setSearchDepth(originalDepth);
    setSearchTimeLimit(originalTimeLimit);
    setUseTranspositionTable(originalTTState);
    setUseOpeningBook(originalBookState);

    CHECK(nodesWithTT < nodesWithoutTT,
          "the transposition table reduces the number of nodes searched at the same depth");
}

static void test_null_move_pruning_toggle(void)
{
    SECTION("setUseNullMovePruning/getUseNullMovePruning");

    bool original = getUseNullMovePruning();

    setUseNullMovePruning(false);
    CHECK(!getUseNullMovePruning(), "setUseNullMovePruning(false) disables it");

    setUseNullMovePruning(true);
    CHECK(getUseNullMovePruning(), "setUseNullMovePruning(true) re-enables it");

    setUseNullMovePruning(original);
}

static void test_null_move_pruning_reduces_node_count(void)
{
    SECTION("null-move pruning measurably reduces nodes searched");

    /* Same non-opening, non-trivial position the transposition-table node-
     * count test above avoids reusing the starting position with - deep
     * enough (6) that negamax actually reaches NULL_MOVE_MIN_DEPTH
     * internally, unlike the shallower TT test above. */
    BoardState board;
    fenToBoard("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", &board);

    int originalDepth = getSearchDepth();
    double originalTimeLimit = getSearchTimeLimit();
    bool originalBookState = getUseOpeningBook();
    bool originalTTState = getUseTranspositionTable();
    bool originalNMPState = getUseNullMovePruning();
    bool originalLMRState = getUseLateMoveReductions();

    setSearchDepth(6);
    setSearchTimeLimit(0);
    setUseOpeningBook(false);
    // The transposition table persists across findBestMove() calls, so
    // comparing "on" vs "off" on the same position in the same process
    // would let the second call ride on the first call's cached results
    // rather than measuring null-move pruning's own effect - disabled here
    // for both runs to isolate it. Likewise for LMR, so it can't mask or
    // compound with NMP's own contribution.
    setUseTranspositionTable(false);
    setUseLateMoveReductions(false);

    setUseNullMovePruning(false);
    findBestMove(&board);
    long nodesWithout = getLastSearchNodeCount();

    fenToBoard("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", &board);
    setUseNullMovePruning(true);
    findBestMove(&board);
    long nodesWith = getLastSearchNodeCount();

    setSearchDepth(originalDepth);
    setSearchTimeLimit(originalTimeLimit);
    setUseOpeningBook(originalBookState);
    setUseTranspositionTable(originalTTState);
    setUseNullMovePruning(originalNMPState);
    setUseLateMoveReductions(originalLMRState);

    CHECK(nodesWith < nodesWithout,
          "null-move pruning reduces the number of nodes searched at the same depth");
}

static void test_late_move_reductions_toggle(void)
{
    SECTION("setUseLateMoveReductions/getUseLateMoveReductions");

    bool original = getUseLateMoveReductions();

    setUseLateMoveReductions(false);
    CHECK(!getUseLateMoveReductions(), "setUseLateMoveReductions(false) disables it");

    setUseLateMoveReductions(true);
    CHECK(getUseLateMoveReductions(), "setUseLateMoveReductions(true) re-enables it");

    setUseLateMoveReductions(original);
}

static void test_late_move_reductions_reduce_node_count(void)
{
    SECTION("late move reductions measurably reduce nodes searched");

    BoardState board;
    fenToBoard("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", &board);

    int originalDepth = getSearchDepth();
    double originalTimeLimit = getSearchTimeLimit();
    bool originalBookState = getUseOpeningBook();
    bool originalTTState = getUseTranspositionTable();
    bool originalNMPState = getUseNullMovePruning();
    bool originalLMRState = getUseLateMoveReductions();

    setSearchDepth(6);
    setSearchTimeLimit(0);
    setUseOpeningBook(false);
    setUseTranspositionTable(false); // see test_null_move_pruning_reduces_node_count() above
    setUseNullMovePruning(false);

    setUseLateMoveReductions(false);
    findBestMove(&board);
    long nodesWithout = getLastSearchNodeCount();

    fenToBoard("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", &board);
    setUseLateMoveReductions(true);
    findBestMove(&board);
    long nodesWith = getLastSearchNodeCount();

    setSearchDepth(originalDepth);
    setSearchTimeLimit(originalTimeLimit);
    setUseOpeningBook(originalBookState);
    setUseTranspositionTable(originalTTState);
    setUseNullMovePruning(originalNMPState);
    setUseLateMoveReductions(originalLMRState);

    CHECK(nodesWith < nodesWithout,
          "late move reductions reduce the number of nodes searched at the same depth");
}

static void test_stable_move_early_exit_toggle(void)
{
    SECTION("setUseStableMoveEarlyExit/getUseStableMoveEarlyExit");

    bool original = getUseStableMoveEarlyExit();

    setUseStableMoveEarlyExit(false);
    CHECK(!getUseStableMoveEarlyExit(), "setUseStableMoveEarlyExit(false) disables it");

    setUseStableMoveEarlyExit(true);
    CHECK(getUseStableMoveEarlyExit(), "setUseStableMoveEarlyExit(true) re-enables it");

    setUseStableMoveEarlyExit(original);
}

static void test_stable_move_early_exit_stops_on_a_confirmed_mate(void)
{
    SECTION("a timed search stops early once a forced mate is confirmed");

    /* Same mate-in-one as test_finds_mate_in_one(). With a large clock the
     * budget is the 30s cap; without the early exit this would run for all
     * of it. */
    BoardState board;
    fenToBoard("7k/5ppp/8/8/8/8/8/3RK3 w - - 0 1", &board);

    bool originalExit = getUseStableMoveEarlyExit();
    bool originalBook = getUseOpeningBook();
    setUseStableMoveEarlyExit(true);
    setUseOpeningBook(false);

    clock_t start = clock();
    Move best = findBestMoveTimed(&board, 3600.0, 0.0);
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;

    setUseStableMoveEarlyExit(originalExit);
    setUseOpeningBook(originalBook);

    CHECK(best.from.row == 7 && best.from.col == 3 && best.to.row == 0 && best.to.col == 3,
          "the early exit still reports the mating move, Rd8#");
    CHECK(elapsed < 5.0, "the search stops long before the 30s budget once the mate is confirmed");
}

static void test_finds_mate_in_one_at_greater_depth(void)
{
    SECTION("findBestMove still finds the same forced mate at a much greater depth");

    /* Same, already-verified mate-in-one as test_finds_mate_in_one() above,
     * but searched much deeper - exercising far more transposition-table
     * hits, killer-move updates, and history-heuristic accumulation than
     * the default-depth test does, while the correct answer is known. */
    BoardState board;
    fenToBoard("7k/5ppp/8/8/8/8/8/3RK3 w - - 0 1", &board);

    int originalDepth = getSearchDepth();
    setSearchDepth(10);

    Move best = findBestMove(&board);
    CHECK(best.from.row == 7 && best.from.col == 3, "findBestMove still picks up the rook on d1");
    CHECK(best.to.row == 0 && best.to.col == 3, "findBestMove still delivers Rd8# at depth 10");

    setSearchDepth(originalDepth);
}

void run_ai_tests(void)
{
    test_finds_mate_in_one();
    test_search_depth_is_adjustable();
    test_search_time_limit_is_adjustable();
    test_time_cap_interrupts_a_deep_search();
    test_compute_move_time_budget();
    test_find_best_move_timed_respects_the_clock();
    test_transposition_table_does_not_change_the_result();
    test_transposition_table_reduces_node_count();
    test_null_move_pruning_toggle();
    test_null_move_pruning_reduces_node_count();
    test_late_move_reductions_toggle();
    test_late_move_reductions_reduce_node_count();
    test_stable_move_early_exit_toggle();
    test_stable_move_early_exit_stops_on_a_confirmed_mate();
    test_finds_mate_in_one_at_greater_depth();
}
