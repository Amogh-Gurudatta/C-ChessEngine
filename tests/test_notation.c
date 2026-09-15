/*
 * Lightweight, dependency-free unit tests for notation.c (long algebraic,
 * SAN, FEN, PGN). No external test framework - just asserts + a summary,
 * consistent with the rest of this project's "no external libs" style.
 *
 * Run with: make test
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "structs.h"
#include "notation.h"
#include "fileio.h"
#include "game.h"
#include "test_common.h"

static void initStartBoard(BoardState *board)
{
    const char *start[8] = {
        "rnbqkbnr", "pppppppp", "........", "........",
        "........", "........", "PPPPPPPP", "RNBQKBNR"};
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            board->squares[r][c] = charToPiece(start[r][c]);

    board->currentPlayer = WHITE;
    board->castling = (CastlingRights){1, 1, 1, 1};
    board->enPassantTarget = (Position){-1, -1};
    board->halfmoveClock = 0;
    board->fullmoveNumber = 1;
}

/* ---------------- Long algebraic ---------------- */

static void test_long_algebraic(void)
{
    SECTION("long algebraic parse/format");

    Move m;
    CHECK(parseLongAlgebraic("e2e4", &m), "parseLongAlgebraic accepts e2e4");
    CHECK(m.from.row == 6 && m.from.col == 4, "e2e4 from-square is e2");
    CHECK(m.to.row == 4 && m.to.col == 4, "e2e4 to-square is e4");

    char buf[8];
    moveToLongAlgebraic(m, buf, sizeof(buf));
    CHECK(strcmp(buf, "e2e4") == 0, "moveToLongAlgebraic round-trips e2e4");

    Move promo;
    CHECK(parseLongAlgebraic("a7a8q", &promo), "parseLongAlgebraic accepts a7a8q");
    CHECK(promo.flag == MOVE_PROMOTION && promo.promotion == QUEEN, "a7a8q parses as queen promotion");
    moveToLongAlgebraic(promo, buf, sizeof(buf));
    CHECK(strcmp(buf, "a7a8q") == 0, "moveToLongAlgebraic round-trips a7a8q");

    Move bad;
    CHECK(!parseLongAlgebraic("e2", &bad), "parseLongAlgebraic rejects a too-short string");

    CHECK(isLongAlgebraicFormat("e2e4"), "isLongAlgebraicFormat accepts e2e4");
    CHECK(isLongAlgebraicFormat("a7a8q"), "isLongAlgebraicFormat accepts a7a8q");
    CHECK(!isLongAlgebraicFormat("e4"), "isLongAlgebraicFormat rejects bare pawn SAN");
    CHECK(!isLongAlgebraicFormat("Nf3"), "isLongAlgebraicFormat rejects piece SAN");
    CHECK(!isLongAlgebraicFormat("O-O"), "isLongAlgebraicFormat rejects castling SAN");
    CHECK(!isLongAlgebraicFormat("e8=Q"), "isLongAlgebraicFormat rejects promotion SAN");
}

/* ---------------- SAN ---------------- */

static void test_san_basic_moves(void)
{
    SECTION("SAN basic moves (start position)");

    BoardState board;
    initStartBoard(&board);

    Move m;
    CHECK(sanToMove(&board, "e4", &m), "sanToMove parses e4");
    CHECK(m.from.row == 6 && m.from.col == 4 && m.to.row == 4 && m.to.col == 4, "e4 resolves to e2-e4");

    char buf[16];
    moveToSan(&board, m, buf, sizeof(buf));
    CHECK(strcmp(buf, "e4") == 0, "moveToSan formats e2-e4 as e4");

    CHECK(sanToMove(&board, "Nf3", &m), "sanToMove parses Nf3");
    CHECK(m.from.row == 7 && m.from.col == 6 && m.to.row == 5 && m.to.col == 5, "Nf3 resolves to g1-f3");
    moveToSan(&board, m, buf, sizeof(buf));
    CHECK(strcmp(buf, "Nf3") == 0, "moveToSan formats g1-f3 as Nf3");

    Move dispatched;
    CHECK(parseUserMove(&board, "e2e4", &dispatched), "parseUserMove accepts long algebraic");
    CHECK(parseUserMove(&board, "Nf3", &dispatched), "parseUserMove accepts SAN");
    CHECK(!parseUserMove(&board, "z9", &dispatched), "parseUserMove rejects garbage input");
}

static void test_san_capture(void)
{
    SECTION("SAN capture");

    /* After 1.e4 d5, white pawn on e4 can capture the black pawn on d5. */
    BoardState board;
    CHECK(fenToBoard("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", &board),
          "fenToBoard parses capture-setup FEN");

    Move m;
    CHECK(sanToMove(&board, "exd5", &m), "sanToMove parses exd5");
    CHECK(m.from.row == 4 && m.from.col == 4 && m.to.row == 3 && m.to.col == 3, "exd5 resolves to e4-d5");

    char buf[16];
    moveToSan(&board, m, buf, sizeof(buf));
    CHECK(strcmp(buf, "exd5") == 0, "moveToSan formats e4xd5 as exd5");
}

static void test_san_disambiguation(void)
{
    SECTION("SAN disambiguation");

    /* Two white knights (a1, c1) can both reach b3. */
    BoardState board;
    CHECK(fenToBoard("4k3/8/8/8/8/8/8/N1N1K3 w - - 0 1", &board), "fenToBoard parses two-knights FEN");

    Move m;
    CHECK(sanToMove(&board, "Nab3", &m), "sanToMove parses file-disambiguated Nab3");
    CHECK(m.from.row == 7 && m.from.col == 0, "Nab3 resolves to the a1 knight");

    char buf[16];
    moveToSan(&board, m, buf, sizeof(buf));
    CHECK(strcmp(buf, "Nab3") == 0, "moveToSan disambiguates by file as Nab3");

    Move ambiguous;
    CHECK(!sanToMove(&board, "Nb3", &ambiguous), "sanToMove rejects ambiguous Nb3 with no disambiguation");
}

static void test_san_castling(void)
{
    SECTION("SAN castling");

    BoardState board;
    CHECK(fenToBoard("4k3/8/8/8/8/8/8/4K2R w K - 0 1", &board), "fenToBoard parses castling-setup FEN");

    Move m;
    CHECK(sanToMove(&board, "O-O", &m), "sanToMove parses O-O");
    CHECK(m.flag == MOVE_CASTLE_KING, "O-O resolves to a king-side castle move");

    char buf[16];
    moveToSan(&board, m, buf, sizeof(buf));
    CHECK(strcmp(buf, "O-O") == 0, "moveToSan formats king-side castle as O-O");
}

static void test_san_promotion(void)
{
    SECTION("SAN promotion");

    BoardState board;
    CHECK(fenToBoard("8/4P3/8/8/3k4/8/8/4K3 w - - 0 1", &board), "fenToBoard parses promotion-setup FEN");

    Move m;
    CHECK(sanToMove(&board, "e8=Q", &m), "sanToMove parses e8=Q");
    CHECK(m.flag == MOVE_PROMOTION && m.promotion == QUEEN, "e8=Q resolves to a queen promotion");

    char buf[16];
    moveToSan(&board, m, buf, sizeof(buf));
    CHECK(strcmp(buf, "e8=Q") == 0, "moveToSan formats promotion as e8=Q (no spurious check)");
}

static void test_san_checkmate(void)
{
    SECTION("SAN checkmate");

    /* Black king boxed in by its own pawns; Rd1-d8 is a back-rank mate. */
    BoardState board;
    CHECK(fenToBoard("7k/5ppp/8/8/8/8/8/3RK3 w - - 0 1", &board), "fenToBoard parses back-rank-mate FEN");

    Move raw, m;
    CHECK(parseLongAlgebraic("d1d8", &raw), "parseLongAlgebraic parses d1d8");
    CHECK(resolveMove(&board, raw, &m), "resolveMove resolves d1d8 against legal moves");

    char buf[16];
    moveToSan(&board, m, buf, sizeof(buf));
    CHECK(strcmp(buf, "Rd8#") == 0, "moveToSan marks back-rank mate with #");
}

/* ---------------- FEN ---------------- */

static void test_fen_start_position(void)
{
    SECTION("FEN start position");

    BoardState board;
    initStartBoard(&board);

    char buf[FEN_MAX_LEN];
    CHECK(boardToFen(&board, buf, sizeof(buf)), "boardToFen succeeds on start position");
    CHECK(strcmp(buf, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") == 0,
          "start position FEN matches the standard string exactly");
}

static void test_fen_roundtrip(void)
{
    SECTION("FEN round-trip");

    const char *fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 2 3";
    BoardState board;
    CHECK(fenToBoard(fen, &board), "fenToBoard parses a mid-game FEN");

    char buf[FEN_MAX_LEN];
    CHECK(boardToFen(&board, buf, sizeof(buf)), "boardToFen succeeds on the parsed board");
    CHECK(strcmp(buf, fen) == 0, "FEN round-trips byte-for-byte");

    const char *fenEnPassant = "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3";
    CHECK(fenToBoard(fenEnPassant, &board), "fenToBoard parses a FEN with an en-passant target");
    CHECK(board.enPassantTarget.row == 2 && board.enPassantTarget.col == 3, "en-passant target d6 parses correctly");
    CHECK(boardToFen(&board, buf, sizeof(buf)), "boardToFen succeeds with an en-passant target");
    CHECK(strcmp(buf, fenEnPassant) == 0, "FEN with en-passant target round-trips byte-for-byte");
}

/* ---------------- Position key / repetition ---------------- */

static int countKeyOccurrences(char keys[][FEN_MAX_LEN], int count, const char *key)
{
    int n = 0;
    for (int i = 0; i < count; i++)
    {
        if (strcmp(keys[i], key) == 0)
            n++;
    }
    return n;
}

static void test_position_key_repetition(void)
{
    SECTION("boardToPositionKey ignores clocks and supports repetition detection");

    /* Faithfully reproduces the exact scenario the real game loop tracks:
     * both kings shuffle back and forth, and the same position (by piece
     * placement/side-to-move/castling/en-passant) recurs a third time,
     * even though the halfmove/fullmove clocks keep advancing throughout. */
    BoardState board;
    CHECK(fenToBoard("7k/8/8/8/8/8/6PP/7K w - - 0 1", &board), "fenToBoard parses the king-shuffle fixture");

    char keys[9][FEN_MAX_LEN];
    boardToPositionKey(&board, keys[0], FEN_MAX_LEN);

    Move shuffle[8] = {
        {{7, 7}, {7, 6}, EMPTY, MOVE_NORMAL}, // Kh1-g1
        {{0, 7}, {0, 6}, EMPTY, MOVE_NORMAL}, // Kh8-g8
        {{7, 6}, {7, 7}, EMPTY, MOVE_NORMAL}, // Kg1-h1
        {{0, 6}, {0, 7}, EMPTY, MOVE_NORMAL}, // Kg8-h8
        {{7, 7}, {7, 6}, EMPTY, MOVE_NORMAL},
        {{0, 7}, {0, 6}, EMPTY, MOVE_NORMAL},
        {{7, 6}, {7, 7}, EMPTY, MOVE_NORMAL},
        {{0, 6}, {0, 7}, EMPTY, MOVE_NORMAL},
    };

    for (int i = 0; i < 8; i++)
    {
        makeMove(&board, shuffle[i]);
        boardToPositionKey(&board, keys[i + 1], FEN_MAX_LEN);
    }

    CHECK(strcmp(keys[0], keys[4]) == 0, "the position after one full king round-trip matches the start");
    CHECK(strcmp(keys[0], keys[8]) == 0, "the position after a second round-trip matches the start again");

    char fenNow[FEN_MAX_LEN], fenStart[FEN_MAX_LEN];
    boardToFen(&board, fenNow, sizeof(fenNow));
    BoardState freshStart;
    fenToBoard("7k/8/8/8/8/8/6PP/7K w - - 0 1", &freshStart);
    boardToFen(&freshStart, fenStart, sizeof(fenStart));
    CHECK(strcmp(fenStart, fenNow) != 0,
          "the halfmove/fullmove clocks differ after the round-trips, proving the key must ignore them");

    CHECK(countKeyOccurrences(keys, 9, keys[0]) == 3,
          "the starting position recurs a third time, which is what triggers a threefold-repetition draw");
}

/* ---------------- PGN ---------------- */

static void test_pgn_export(void)
{
    SECTION("PGN export");

    char sanLog[4][SAN_MAX_LEN];
    strcpy(sanLog[0], "e4");
    strcpy(sanLog[1], "e5");
    strcpy(sanLog[2], "Nf3");
    strcpy(sanLog[3], "Nc6");

    const char *path = "build/test_output.pgn";
    CHECK(exportPgn(path, sanLog, 4, "TestWhite", "TestBlack", "1-0"), "exportPgn writes the file");

    FILE *f = fopen(path, "r");
    CHECK(f != NULL, "exported PGN file can be reopened");
    if (f != NULL)
    {
        char contents[2048];
        size_t n = fread(contents, 1, sizeof(contents) - 1, f);
        contents[n] = '\0';
        fclose(f);
        remove(path);

        CHECK(strstr(contents, "[White \"TestWhite\"]") != NULL, "PGN contains the White tag");
        CHECK(strstr(contents, "[Black \"TestBlack\"]") != NULL, "PGN contains the Black tag");
        CHECK(strstr(contents, "[Result \"1-0\"]") != NULL, "PGN contains the Result tag");
        CHECK(strstr(contents, "1. e4 e5 2. Nf3 Nc6") != NULL, "PGN move text is correctly numbered");
    }
}

void run_notation_tests(void)
{
    test_long_algebraic();
    test_san_basic_moves();
    test_san_capture();
    test_san_disambiguation();
    test_san_castling();
    test_san_promotion();
    test_san_checkmate();
    test_fen_start_position();
    test_fen_roundtrip();
    test_position_key_repetition();
    test_pgn_export();
}
