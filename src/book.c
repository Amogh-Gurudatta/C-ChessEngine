#include "book.h"
#include "structs.h"
#include "game.h"
#include "fileio.h"

#include <string.h>
#include <stddef.h>

/* ============================================================================
 * OPENING BOOK DATA
 * ============================================================================
 * Each line is a NULL-terminated array of moves in long algebraic notation
 * ("e2e4"), the same format used everywhere else in the engine (UCI,
 * Lichess). Lines are kept to a handful of moves - well inside established
 * theory, and deliberately short of any castling, promotion, or en passant
 * capture (see the module doc comment in book.h / docs/OPENING_BOOK.md for
 * why that keeps every book move a plain MOVE_NORMAL move, which is what
 * lets this file build its lookup table with nothing but game.c's makeMove()
 * - no move generation or legality checking needed here at all).
 *
 * Every line below was verified move-by-move against the engine's own
 * generateAllLegalMoves() before being added here.
 */

static const char *const italianGame[] = {
    "e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "c2c3", "g8f6", NULL};
static const char *const ruyLopez[] = {
    "e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6", NULL};
static const char *const scotchGame[] = {
    "e2e4", "e7e5", "g1f3", "b8c6", "d2d4", "e5d4", "f3d4", "g8f6", NULL};
static const char *const petrovDefense[] = {
    "e2e4", "e7e5", "g1f3", "g8f6", "f3e5", "d7d6", "e5f3", "f6e4", NULL};
static const char *const philidorDefense[] = {
    "e2e4", "e7e5", "g1f3", "d7d6", "d2d4", "g8f6", NULL};
static const char *const viennaGame[] = {
    "e2e4", "e7e5", "b1c3", "g8f6", "f2f4", NULL};
static const char *const sicilianOpen[] = {
    "e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6", "b1c3", "a7a6", NULL};
static const char *const sicilianClosed[] = {
    "e2e4", "c7c5", "b1c3", "b8c6", "g2g3", "g7g6", NULL};
static const char *const frenchDefense[] = {
    "e2e4", "e7e6", "d2d4", "d7d5", "b1c3", "g8f6", "c1g5", "f8e7", NULL};
static const char *const caroKannDefense[] = {
    "e2e4", "c7c6", "d2d4", "d7d5", "b1c3", "d5e4", "c3e4", "c8f5", NULL};
static const char *const scandinavianDefense[] = {
    "e2e4", "d7d5", "e4d5", "d8d5", "b1c3", "d5a5", NULL};
static const char *const queensGambitDeclined[] = {
    "d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "g8f6", "c1g5", "f8e7", NULL};
static const char *const queensGambitAccepted[] = {
    "d2d4", "d7d5", "c2c4", "d5c4", "g1f3", "g8f6", NULL};
static const char *const slavDefense[] = {
    "d2d4", "d7d5", "c2c4", "c7c6", "g1f3", "g8f6", "b1c3", "d5c4", NULL};
static const char *const kingsIndianDefense[] = {
    "d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "f8g7", "e2e4", "d7d6", NULL};
static const char *const nimzoIndianDefense[] = {
    "d2d4", "g8f6", "c2c4", "e7e6", "b1c3", "f8b4", NULL};
static const char *const grunfeldDefense[] = {
    "d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "d7d5", NULL};
static const char *const dutchDefense[] = {
    "d2d4", "f7f5", "g2g3", "g8f6", "f1g2", "e7e6", NULL};
static const char *const londonSystem[] = {
    "d2d4", "d7d5", "g1f3", "g8f6", "c1f4", "e7e6", NULL};
static const char *const englishOpening[] = {
    "c2c4", "e7e5", "b1c3", "g8f6", "g1f3", "b8c6", NULL};
static const char *const retiOpening[] = {
    "g1f3", "d7d5", "c2c4", "e7e6", "g2g3", NULL};

static const char *const *const bookLines[] = {
    italianGame, ruyLopez, scotchGame, petrovDefense, philidorDefense,
    viennaGame, sicilianOpen, sicilianClosed, frenchDefense, caroKannDefense,
    scandinavianDefense, queensGambitDeclined, queensGambitAccepted,
    slavDefense, kingsIndianDefense, nimzoIndianDefense, grunfeldDefense,
    dutchDefense, londonSystem, englishOpening, retiOpening};
#define BOOK_LINE_COUNT (sizeof(bookLines) / sizeof(bookLines[0]))

/* Deepest line above is 10 plies (5 full moves); this only needs to be an
 * upper bound, not exact, so it comfortably covers that plus room for
 * future, slightly longer lines without letting the book linger and get
 * checked deep into an unrelated middlegame. */
#define BOOK_MAX_FULLMOVE 10

/* ============================================================================
 * LOOKUP TABLE (built lazily, once, from the lines above)
 * ============================================================================ */

/* Piece placement (64 chars, fileio.h's pieceToChar()) plus a side-to-move
 * char is enough to uniquely key a position for this purpose: two lines
 * transposing into an identical piece arrangement with the same side to
 * move should get the same suggested continuation regardless of the move
 * order that reached it - that's what "transposition" means in chess, and
 * matching on it for free is a nice side effect of this key rather than
 * something that needed extra work. It ignores castling rights/en passant,
 * which in the extremely rare case of two different histories reaching
 * identical piece placement but different castling rights could suggest a
 * move that's technically not what a stricter key would have found - not a
 * concern for a short, curated opening book like this one (see
 * docs/OPENING_BOOK.md). */
#define BOOK_KEY_LEN 66 // 64 squares + 1 side-to-move char + '\0'
typedef struct
{
    char key[BOOK_KEY_LEN];
    Move move;
} BookEntry;

/* Total plies across every line above, rounded up with headroom for
 * additions. */
#define MAX_BOOK_ENTRIES 256
static BookEntry bookTable[MAX_BOOK_ENTRIES];
static int bookEntryCount = 0;
static bool bookBuilt = false;
static bool useOpeningBook = true;

static void buildPositionKey(const BoardState *board, char key[BOOK_KEY_LEN])
{
    int i = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            key[i++] = pieceToChar(board->squares[r][c]);
    key[i++] = (board->currentPlayer == WHITE) ? 'w' : 'b';
    key[i] = '\0';
}

/* Every book move is a plain move between two squares with no promotion -
 * see the file doc comment above for why the curated lines never need
 * anything else. */
static Move parseBookMove(const char *s)
{
    Move m;
    m.from.col = s[0] - 'a';
    m.from.row = 8 - (s[1] - '0');
    m.to.col = s[2] - 'a';
    m.to.row = 8 - (s[3] - '0');
    m.promotion = EMPTY;
    m.flag = MOVE_NORMAL;
    return m;
}

/* A local standard starting position, independent of main.c's/lichess.c's
 * own copies of the same literal - see docs/OPENING_BOOK.md for why book.c
 * deliberately doesn't reach for notation.c's fenToBoard() to build this. */
static void resetToStartPosition(BoardState *board)
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

static void addLine(const char *const *moves)
{
    BoardState board;
    resetToStartPosition(&board);

    for (int i = 0; moves[i] != NULL; i++)
    {
        if (bookEntryCount >= MAX_BOOK_ENTRIES)
            return; // safety net; MAX_BOOK_ENTRIES already sizes for this

        Move m = parseBookMove(moves[i]);
        buildPositionKey(&board, bookTable[bookEntryCount].key);
        bookTable[bookEntryCount].move = m;
        bookEntryCount++;

        makeMove(&board, m);
    }
}

static void ensureBookBuilt(void)
{
    if (bookBuilt)
        return;
    for (size_t i = 0; i < BOOK_LINE_COUNT; i++)
        addLine(bookLines[i]);
    bookBuilt = true;
}

bool findBookMove(const BoardState *board, Move *out)
{
    if (!useOpeningBook || board->fullmoveNumber > BOOK_MAX_FULLMOVE)
        return false;

    ensureBookBuilt();

    char key[BOOK_KEY_LEN];
    buildPositionKey(board, key);

    for (int i = 0; i < bookEntryCount; i++)
    {
        if (strcmp(bookTable[i].key, key) == 0)
        {
            *out = bookTable[i].move;
            return true;
        }
    }
    return false;
}

void setUseOpeningBook(bool enabled)
{
    useOpeningBook = enabled;
}

bool getUseOpeningBook(void)
{
    return useOpeningBook;
}
