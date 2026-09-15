#include <stdio.h>
#include <string.h>

#include "structs.h"
#include "timecontrol.h"
#include "test_common.h"

static void test_clock_init(void)
{
    SECTION("clockInit");

    ChessClock clock;
    clockInit(&clock, 300.0, 3.0);
    CHECK(clock.whiteSeconds == 300.0, "clockInit sets White's starting time");
    CHECK(clock.blackSeconds == 300.0, "clockInit sets Black's starting time");
    CHECK(clock.incrementSeconds == 3.0, "clockInit sets the increment");
}

static void test_clock_consume_and_increment(void)
{
    SECTION("clockConsume applies elapsed time and increment");

    ChessClock clock;
    clockInit(&clock, 60.0, 2.0);

    bool stillOk = clockConsume(&clock, WHITE, 10.0);
    CHECK(stillOk, "consuming less time than remains does not flag the side");
    CHECK(clock.whiteSeconds == 52.0, "remaining time is (60 - 10 + 2) after one move");
    CHECK(clock.blackSeconds == 60.0, "consuming White's time leaves Black's untouched");

    stillOk = clockConsume(&clock, BLACK, 5.0);
    CHECK(stillOk, "Black also gains the increment after a normal move");
    CHECK(clock.blackSeconds == 57.0, "Black's remaining time is (60 - 5 + 2)");
}

static void test_clock_flag_fall(void)
{
    SECTION("clockConsume detects flag-fall and withholds the increment");

    ChessClock clock;
    clockInit(&clock, 5.0, 3.0);

    bool stillOk = clockConsume(&clock, WHITE, 10.0);
    CHECK(!stillOk, "spending more time than remains flags the side");
    CHECK(clockHasFlagged(&clock, WHITE), "clockHasFlagged reports the flagged side");
    CHECK(clock.whiteSeconds < 0, "no increment is granted once a side has already flagged");
    CHECK(!clockHasFlagged(&clock, BLACK), "the other side is unaffected");
}

static void test_clock_format(void)
{
    SECTION("clockFormat");

    char buf[16];

    clockFormat(0.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "0:00") == 0, "0 seconds formats as 0:00");

    clockFormat(65.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "1:05") == 0, "65 seconds formats as 1:05");

    clockFormat(3725.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "1:02:05") == 0, "3725 seconds formats with an hour component as 1:02:05");

    clockFormat(-5.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "0:00") == 0, "a negative (already-flagged) time formats as 0:00, not garbage");
}

void run_timecontrol_tests(void)
{
    test_clock_init();
    test_clock_consume_and_increment();
    test_clock_flag_fall();
    test_clock_format();
}
