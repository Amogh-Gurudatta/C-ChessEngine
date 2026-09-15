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

static void test_bishop_pair_bonus(void)
{
    SECTION("evaluateBoard rewards the bishop pair");

    // Both sides have exactly two minor pieces; only White's combination
    // (two bishops vs. a bishop and a knight) differs, with kings mirrored
    // on the same file so the control case is perfectly symmetric.
    BoardState withPair;
    fenToBoard("2bnk3/8/8/8/8/8/8/2BBK3 w - - 0 1", &withPair);
    BoardState control;
    fenToBoard("2bnk3/8/8/8/8/8/8/2BNK3 w - - 0 1", &control);

    CHECK(evaluateBoard(&control) == 0, "a fully mirrored bishop+knight-vs-bishop+knight position is exactly balanced");
    CHECK(evaluateBoard(&withPair) > evaluateBoard(&control),
          "having the bishop pair evaluates better than a lone bishop and knight");
}

static void test_rook_open_file_bonus(void)
{
    SECTION("evaluateBoard rewards a rook on an open file");

    // Both positions have exactly one white rook and identical pawns
    // otherwise; removing the symmetric pair of e-file pawns opens the
    // rook's file without changing the material balance (a white pawn and
    // a black pawn of equal value are removed together).
    BoardState openFile;
    fenToBoard("4k3/pppp1ppp/8/8/8/8/PPPP1PPP/4RK2 w - - 0 1", &openFile);
    BoardState closedFile;
    fenToBoard("4k3/pppppppp/8/8/8/8/PPPPPPPP/4RK2 w - - 0 1", &closedFile);

    CHECK(evaluateBoard(&openFile) > evaluateBoard(&closedFile),
          "a rook on a fully open file evaluates better than the same rook on a closed file");
}

static void test_doubled_pawns_penalty(void)
{
    SECTION("evaluateBoard penalizes doubled pawns");

    // Same pawn count (2) in both positions; only whether they share a
    // file differs.
    BoardState doubled;
    fenToBoard("4k3/8/8/8/8/3P4/3P4/4K3 w - - 0 1", &doubled);
    BoardState spread;
    fenToBoard("4k3/8/8/8/8/8/3PP3/4K3 w - - 0 1", &spread);

    CHECK(evaluateBoard(&doubled) < evaluateBoard(&spread),
          "two pawns doubled on the same file evaluate worse than the same two pawns spread across files");
}

static void test_isolated_pawns_penalty(void)
{
    SECTION("evaluateBoard penalizes isolated pawns");

    // Same pawn count (2) in both positions; only whether they support
    // each other (adjacent files) differs.
    BoardState isolated;
    fenToBoard("4k3/8/8/8/2P1P3/8/8/4K3 w - - 0 1", &isolated);
    BoardState connected;
    fenToBoard("4k3/8/8/8/3PP3/8/8/4K3 w - - 0 1", &connected);

    CHECK(evaluateBoard(&isolated) < evaluateBoard(&connected),
          "two pawns with a gap between them evaluate worse than two mutually-supporting pawns");
}

static void test_passed_pawn_bonus(void)
{
    SECTION("evaluateBoard rewards a passed pawn");

    // Same material (1 white pawn + 1 black pawn) in both positions; only
    // whether the black pawn actually blocks/contests the white pawn's
    // path to promotion differs.
    BoardState passed;
    fenToBoard("k7/p7/4P3/8/8/8/8/4K3 w - - 0 1", &passed);
    BoardState blocked;
    fenToBoard("4k3/4p3/4P3/8/8/8/8/4K3 w - - 0 1", &blocked);

    CHECK(evaluateBoard(&passed) > evaluateBoard(&blocked),
          "an unopposed passed pawn evaluates better than the same pawn blocked by an enemy pawn ahead of it");
}

static void test_king_shield_bonus(void)
{
    SECTION("evaluateBoard rewards an intact king-side pawn shield");

    // Same material (1 knight + 3 pawns per side) in both positions, with
    // a knight each to keep the game phase above 0 (this bonus is
    // middlegame-only by design); only whether White's own pawns actually
    // shield White's own king differs. Black's pawns never shield Black's
    // king in either case, holding that side constant.
    BoardState shielded;
    fenToBoard("1n2k3/ppp5/8/8/8/8/5PPP/1N4K1 w - - 0 1", &shielded);
    BoardState exposed;
    fenToBoard("1n2k3/ppp5/8/8/8/8/PPP5/1N4K1 w - - 0 1", &exposed);

    CHECK(evaluateBoard(&shielded) > evaluateBoard(&exposed),
          "a king with pawns still in front of it evaluates better than the same king with its pawns elsewhere");
}

void run_eval_tests(void)
{
    test_start_position_is_balanced();
    test_material_imbalance();
    test_game_phase();
    test_bishop_pair_bonus();
    test_rook_open_file_bonus();
    test_doubled_pawns_penalty();
    test_isolated_pawns_penalty();
    test_passed_pawn_bonus();
    test_king_shield_bonus();
}
