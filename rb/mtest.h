/* ==========================================================================
    Licensed under BSD 2clause license. See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */


/* ==== mtest version v0.1.0 ================================================ */


/* ==========================================================================
    Tests uses simple TAP output format (http://testanything.org)
   ========================================================================== */


/* ==== include files ======================================================= */


#include <stdio.h>


/* ==== public macros ======================================================= */


/* ==========================================================================
    macro with definitions, call this macro no more and no less than ONCE in
    global scope. Its task is to define some variables used by mtest macros.
   ========================================================================== */


#define mt_defs()                                                              \
    const char *curr_test;                                                     \
    int mt_test_status;                                                        \
    int mt_total_tests = 0;                                                    \
    int mt_total_failed = 0


/* ==========================================================================
    macro runs test 'f'. 'f' is just a function (without parenthesis ()).
   ========================================================================== */


#define mt_run(f) do {                                                         \
    curr_test = #f;                                                            \
    mt_test_status = 0;                                                        \
    ++mt_total_tests;                                                          \
    f();                                                                       \
    if (mt_test_status != 0)                                                   \
    {                                                                          \
        fprintf(stdout, "not ok %d - %s\n", mt_total_tests, curr_test);        \
        ++mt_total_failed;                                                     \
    }                                                                          \
    else                                                                       \
        fprintf(stdout, "ok %d - %s\n", mt_total_tests, curr_test);            \
    } while(0)


/* ==========================================================================
    simple assert, when expression 'e' is evaluated to false, assert message
    will be logged, and macro will force function to return
   ========================================================================== */


#define mt_assert(e) do {                                                      \
    if (!(e))                                                                  \
    {                                                                          \
        fprintf(stdout, "# assert %d: %s, %s\n", __LINE__, curr_test, #e);     \
        mt_test_status = -1;                                                   \
        return;                                                                \
    } } while (0)


/* ==========================================================================
    same as mt_assert, but function is not forced to return, and test can
    continue
   ========================================================================== */


#define mt_fail(e) do {                                                        \
    if (!(e))                                                                  \
    {                                                                          \
        fprintf(stdout, "# assert %d: %s, %s\n", __LINE__, curr_test, #e);     \
        mt_test_status = -1;                                                   \
    } } while(0)


/* ==========================================================================
    prints test plan, in format 1..<number_of_test_run>. If all tests have
    passed, macro will return current function with code 0, else it returns
    number of failed tests. If number of failed tests exceeds 254, then 254
    will be returned
   ========================================================================== */


#define mt_return() do {                                                       \
    fprintf(stdout, "1..%d\n", mt_total_tests);                                \
    return mt_total_failed > 254 ? 254 : mt_total_failed; } while(0)
