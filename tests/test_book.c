#include <stdio.h>
#include <string.h>

#include "structs.h"
#include "ai.h"
#include "book.h"
#include "game.h"
#include "notation.h"
#include "test_common.h"

static bool moveIs(Move m, const char *longAlgebraic)
{
    Move parsed;
    parseLongAlgebraic(longAlgebraic, &parsed);
    return m.from.row == parsed.from.row && m.from.col == parsed.from.col &&
           m.to.row == parsed.to.row && m.to.col == parsed.to.col;
}

static void test_book_matches_the_starting_position(void)
{
    SECTION("findBookMove suggests a known reply from the starting position");

    bool originalState = getUseOpeningBook();
    setUseOpeningBook(true);

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    Move book;
    bool found = findBookMove(&board, &book);
    CHECK(found, "the starting position is covered by the book");
    CHECK(moveIs(book, "e2e4") || moveIs(book, "d2d4") || moveIs(book, "c2c4") || moveIs(book, "g1f3"),
          "the suggested first move is one of this engine's actual book openings");

    setUseOpeningBook(originalState);
}

static void test_book_follows_a_known_line(void)
{
    SECTION("findBookMove keeps matching while a real opening line is played out");

    bool originalState = getUseOpeningBook();
    setUseOpeningBook(true);

    /* Replay the Ruy Lopez exactly as authored in book.c, applying each
     * suggested move to a live BoardState via the real move pipeline -
     * this both confirms the book stays "in book" move after move and that
     * every move it suggests is fully legal (findBestMove already
     * re-validates against generateAllLegalMoves(), but resolving it here
     * independently via notation.c is a second, differently-implemented
     * check). */
    const char *expected[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6"};
    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    int matched = 0;
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        Move book;
        if (!findBookMove(&board, &book))
            break;

        Move raw, resolved;
        if (!parseLongAlgebraic(expected[i], &raw))
            break;
        if (book.from.row != raw.from.row || book.from.col != raw.from.col ||
            book.to.row != raw.to.row || book.to.col != raw.to.col)
            break; // this position's book entry diverged from the Ruy Lopez - fine, stop here

        if (!resolveMove(&board, raw, &resolved))
            break;
        makeMove(&board, resolved);
        matched++;
    }

    CHECK(matched >= 4, "the book follows at least the first few moves of a real opening line");

    setUseOpeningBook(originalState);
}

static void test_book_misses_an_unrelated_position(void)
{
    SECTION("findBookMove reports no match for a position no book line reaches");

    bool originalState = getUseOpeningBook();
    setUseOpeningBook(true);

    /* An endgame position that no 5-move-deep opening line could ever
     * transpose into. */
    BoardState board;
    fenToBoard("8/8/4k3/8/8/4K3/4P3/8 w - - 0 1", &board);

    Move book;
    CHECK(!findBookMove(&board, &book), "an unrelated endgame position is not in the book");

    setUseOpeningBook(originalState);
}

static void test_book_respects_the_enable_toggle(void)
{
    SECTION("setUseOpeningBook/getUseOpeningBook");

    bool originalState = getUseOpeningBook();

    setUseOpeningBook(false);
    CHECK(!getUseOpeningBook(), "setUseOpeningBook(false) disables the book");

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    Move book;
    CHECK(!findBookMove(&board, &book), "a disabled book never returns a move, even for a known position");

    setUseOpeningBook(true);
    CHECK(getUseOpeningBook(), "setUseOpeningBook(true) re-enables the book");
    CHECK(findBookMove(&board, &book), "the book matches again once re-enabled");

    setUseOpeningBook(originalState);
}

static void test_find_best_move_plays_a_book_move_instantly(void)
{
    SECTION("findBestMove returns a book move without searching, when the book is enabled");

    bool originalBookState = getUseOpeningBook();
    int originalDepth = getSearchDepth();
    double originalTimeLimit = getSearchTimeLimit();

    setUseOpeningBook(true);
    setSearchDepth(20);      // would take a very long time if this actually searched
    setSearchTimeLimit(0);   // uncapped: if this isn't a book move, the test would hang

    BoardState board;
    fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    Move book;
    findBookMove(&board, &book);
    Move best = findBestMove(&board);

    CHECK(best.from.row == book.from.row && best.from.col == book.from.col &&
              best.to.row == book.to.row && best.to.col == book.to.col,
          "findBestMove returns exactly the book's suggested move when one is available");
    CHECK(getLastSearchNodeCount() == 0, "a book move is returned without visiting any search nodes");

    setUseOpeningBook(originalBookState);
    setSearchDepth(originalDepth);
    setSearchTimeLimit(originalTimeLimit);
}

void run_book_tests(void)
{
    test_book_matches_the_starting_position();
    test_book_follows_a_known_line();
    test_book_misses_an_unrelated_position();
    test_book_respects_the_enable_toggle();
    test_find_best_move_plays_a_book_move_instantly();
}
