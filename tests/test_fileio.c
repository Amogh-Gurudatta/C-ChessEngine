#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "structs.h"
#include "fileio.h"
#include "notation.h"
#include "test_common.h"

static void test_piece_char_roundtrip(void)
{
    SECTION("pieceToChar / charToPiece round-trip");

    struct
    {
        PieceType type;
        char whiteChar;
        char blackChar;
    } cases[] = {
        {PAWN, 'P', 'p'},
        {KNIGHT, 'N', 'n'},
        {BISHOP, 'B', 'b'},
        {ROOK, 'R', 'r'},
        {QUEEN, 'Q', 'q'},
        {KING, 'K', 'k'},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        Piece white = {cases[i].type, WHITE};
        Piece black = {cases[i].type, BLACK};
        CHECK(pieceToChar(white) == cases[i].whiteChar, "pieceToChar formats the white piece correctly");
        CHECK(pieceToChar(black) == cases[i].blackChar, "pieceToChar formats the black piece correctly");

        Piece parsedWhite = charToPiece(cases[i].whiteChar);
        Piece parsedBlack = charToPiece(cases[i].blackChar);
        CHECK(parsedWhite.type == cases[i].type && parsedWhite.color == WHITE,
              "charToPiece parses the white piece correctly");
        CHECK(parsedBlack.type == cases[i].type && parsedBlack.color == BLACK,
              "charToPiece parses the black piece correctly");
    }

    Piece empty = charToPiece('.');
    CHECK(empty.type == EMPTY, "charToPiece('.') yields EMPTY");
    CHECK(pieceToChar((Piece){EMPTY, NO_COLOR}) == '.', "pieceToChar(EMPTY) yields '.'");
}

static void test_save_load_roundtrip(void)
{
    SECTION("saveBoardToFile / loadBoardFromFile round-trip");

    BoardState board;
    CHECK(fenToBoard("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 2 3", &board),
          "fenToBoard builds a fixture position");

    const char *path = "build/test_save.txt";
    CHECK(saveBoardToFile(path, &board), "saveBoardToFile writes the file");

    BoardState loaded;
    CHECK(loadBoardFromFile(path, &loaded), "loadBoardFromFile reads it back");
    remove(path);

    char fenBefore[FEN_MAX_LEN], fenAfter[FEN_MAX_LEN];
    boardToFen(&board, fenBefore, sizeof(fenBefore));
    boardToFen(&loaded, fenAfter, sizeof(fenAfter));
    CHECK(strcmp(fenBefore, fenAfter) == 0, "save/load round-trip preserves the position exactly");
}

static void test_load_missing_file(void)
{
    SECTION("loadBoardFromFile on a missing file");

    BoardState board;
    CHECK(!loadBoardFromFile("build/does_not_exist.txt", &board),
          "loadBoardFromFile fails gracefully for a missing file");
}

void run_fileio_tests(void)
{
    test_piece_char_roundtrip();
    test_save_load_roundtrip();
    test_load_missing_file();
}
