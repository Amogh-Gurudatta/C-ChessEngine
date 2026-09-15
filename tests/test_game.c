#include <stdio.h>
#include <stdbool.h>

#include "structs.h"
#include "game.h"
#include "notation.h"
#include "test_common.h"

static void test_normal_move(void)
{
    SECTION("makeMove/undoMove: normal move (double pawn push)");

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    Move m = {{6, 4}, {4, 4}, EMPTY, MOVE_NORMAL}; // e2-e4
    makeMove(&board, m);

    CHECK(board.squares[4][4].type == PAWN && board.squares[4][4].color == WHITE, "pawn lands on e4");
    CHECK(board.squares[6][4].type == EMPTY, "e2 is empty after the push");
    CHECK(board.currentPlayer == BLACK, "side to move flips to Black");
    CHECK(board.enPassantTarget.row == 5 && board.enPassantTarget.col == 4,
          "double push sets the en-passant target to e3");
    CHECK(board.halfmoveClock == 0, "pawn move resets the halfmove clock");

    undoMove(&board, m);
    CHECK(board.squares[6][4].type == PAWN, "undo restores the pawn to e2");
    CHECK(board.squares[4][4].type == EMPTY, "undo clears e4");
    CHECK(board.currentPlayer == WHITE, "undo restores side to move");
    CHECK(board.enPassantTarget.row == -1, "undo clears the en-passant target");
}

static void test_capture_resets_halfmove(void)
{
    SECTION("makeMove/undoMove: capture resets halfmove clock");

    BoardState board;
    fenToBoard("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 5 3", &board);

    Move m = {{4, 4}, {3, 3}, EMPTY, MOVE_NORMAL}; // exd5
    makeMove(&board, m);

    CHECK(board.squares[3][3].type == PAWN && board.squares[3][3].color == WHITE, "white pawn lands on d5");
    CHECK(board.halfmoveClock == 0, "capture resets the halfmove clock");

    undoMove(&board, m);
    CHECK(board.squares[3][3].type == PAWN && board.squares[3][3].color == BLACK,
          "undo restores the captured black pawn");
    CHECK(board.halfmoveClock == 5, "undo restores the previous halfmove clock");
}

static void test_en_passant(void)
{
    SECTION("makeMove/undoMove: en passant capture");

    BoardState board;
    fenToBoard("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3", &board);

    Move m = {{3, 4}, {2, 3}, EMPTY, MOVE_EN_PASSANT}; // exd6 e.p.
    makeMove(&board, m);

    CHECK(board.squares[2][3].type == PAWN && board.squares[2][3].color == WHITE, "capturing pawn lands on d6");
    CHECK(board.squares[3][3].type == EMPTY, "the captured black pawn on d5 is removed");

    undoMove(&board, m);
    CHECK(board.squares[3][3].type == PAWN && board.squares[3][3].color == BLACK,
          "undo restores the captured pawn on d5");
    CHECK(board.squares[3][4].type == PAWN && board.squares[3][4].color == WHITE,
          "undo restores the white pawn to e5");
    CHECK(board.squares[2][3].type == EMPTY, "undo clears d6");
}

static void test_promotion(void)
{
    SECTION("makeMove/undoMove: promotion");

    BoardState board;
    fenToBoard("8/4P3/8/8/3k4/8/8/4K3 w - - 0 1", &board);

    Move m = {{1, 4}, {0, 4}, QUEEN, MOVE_PROMOTION};
    makeMove(&board, m);
    CHECK(board.squares[0][4].type == QUEEN && board.squares[0][4].color == WHITE, "pawn promotes to a queen");

    undoMove(&board, m);
    CHECK(board.squares[1][4].type == PAWN && board.squares[1][4].color == WHITE, "undo restores the pawn");
    CHECK(board.squares[0][4].type == EMPTY, "undo clears the promotion square");
}

static void test_castling_kingside_white(void)
{
    SECTION("makeMove/undoMove: white king-side castling");

    BoardState board;
    fenToBoard("4k3/8/8/8/8/8/8/4K2R w K - 0 1", &board);

    Move m = {{7, 4}, {7, 6}, EMPTY, MOVE_CASTLE_KING};
    makeMove(&board, m);

    CHECK(board.squares[7][6].type == KING, "king lands on g1");
    CHECK(board.squares[7][5].type == ROOK, "rook lands on f1");
    CHECK(board.squares[7][7].type == EMPTY, "h1 is empty after castling");
    CHECK(!board.castling.wk && !board.castling.wq, "castling rights are revoked for White");

    undoMove(&board, m);
    CHECK(board.squares[7][4].type == KING, "undo restores the king to e1");
    CHECK(board.squares[7][7].type == ROOK, "undo restores the rook to h1");
    CHECK(board.castling.wk, "undo restores king-side castling rights");
}

static void test_castling_queenside_black(void)
{
    SECTION("makeMove/undoMove: black queen-side castling");

    BoardState board;
    fenToBoard("r3k3/8/8/8/8/8/8/4K3 b q - 0 1", &board);

    Move m = {{0, 4}, {0, 2}, EMPTY, MOVE_CASTLE_QUEEN};
    makeMove(&board, m);

    CHECK(board.squares[0][2].type == KING, "king lands on c8");
    CHECK(board.squares[0][3].type == ROOK, "rook lands on d8");
    CHECK(board.squares[0][0].type == EMPTY, "a8 is empty after castling");

    undoMove(&board, m);
    CHECK(board.squares[0][4].type == KING, "undo restores the king to e8");
    CHECK(board.squares[0][0].type == ROOK, "undo restores the rook to a8");
}

static void test_rook_capture_revokes_castling_rights(void)
{
    SECTION("makeMove/undoMove: capturing a rook on its home square revokes castling rights");

    BoardState board;
    fenToBoard("4k2r/8/8/8/8/8/8/B3K3 w Kk - 0 1", &board);

    Move m = {{7, 0}, {0, 7}, EMPTY, MOVE_NORMAL}; // bishop a1xh8
    makeMove(&board, m);

    CHECK(!board.castling.bk, "capturing the h8 rook revokes Black's king-side right");
    CHECK(board.castling.wk, "White's own right is untouched");

    undoMove(&board, m);
    CHECK(board.castling.bk, "undo restores Black's king-side right");
}

static void test_fullmove_counter(void)
{
    SECTION("makeMove: fullmove counter increments after Black moves");

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", &board);
    CHECK(board.fullmoveNumber == 1, "fixture starts at fullmove 1");

    Move m = {{1, 4}, {3, 4}, EMPTY, MOVE_NORMAL}; // ...e5
    makeMove(&board, m);
    CHECK(board.fullmoveNumber == 2, "fullmove increments once Black has moved");
}

static void test_attack_detection(void)
{
    SECTION("isSquareAttacked / isKingInCheck");

    BoardState rookOpen;
    fenToBoard("4k3/8/8/8/8/8/8/R3K3 w - - 0 1", &rookOpen);
    CHECK(isSquareAttacked(&rookOpen, 0, 0, WHITE), "rook on a1 attacks along the open a-file up to a8");
    CHECK(!isKingInCheck(&rookOpen, BLACK), "black king on e8 is off the a-file, not in check");

    BoardState rookBlocked;
    fenToBoard("4k3/8/8/8/8/8/p7/R3K3 w - - 0 1", &rookBlocked);
    CHECK(!isSquareAttacked(&rookBlocked, 0, 0, WHITE), "a black pawn on a2 blocks the rook's line of sight to a8");

    BoardState bishopCheck;
    fenToBoard("7k/8/8/8/4B3/8/8/4K3 w - - 0 1", &bishopCheck);
    CHECK(isSquareAttacked(&bishopCheck, 0, 0, WHITE), "bishop on e4 attacks a8 along the a8-h1 diagonal");

    BoardState knightCheck;
    fenToBoard("4k3/8/3N4/8/8/8/8/4K3 w - - 0 1", &knightCheck);
    CHECK(isKingInCheck(&knightCheck, BLACK), "knight on d6 checks the king on e8");

    BoardState pawnCheck;
    fenToBoard("4k3/3P4/8/8/8/8/8/4K3 w - - 0 1", &pawnCheck);
    CHECK(isKingInCheck(&pawnCheck, BLACK), "white pawn on d7 attacks e8 diagonally");
}

void run_game_tests(void)
{
    test_normal_move();
    test_capture_resets_halfmove();
    test_en_passant();
    test_promotion();
    test_castling_kingside_white();
    test_castling_queenside_black();
    test_rook_capture_revokes_castling_rights();
    test_fullmove_counter();
    test_attack_detection();
}
