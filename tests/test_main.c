#include <stdio.h>

#include "test_common.h"

void run_fileio_tests(void);
void run_game_tests(void);
void run_movegen_tests(void);
void run_eval_tests(void);
void run_ai_tests(void);
void run_book_tests(void);
void run_notation_tests(void);
void run_timecontrol_tests(void);

int main(void)
{
    run_fileio_tests();
    run_game_tests();
    run_movegen_tests();
    run_eval_tests();
    run_ai_tests();
    run_book_tests();
    run_notation_tests();
    run_timecontrol_tests();

    printf("\n%d/%d tests passed\n", testsRun - testsFailed, testsRun);
    return testsFailed == 0 ? 0 : 1;
}
