/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */

/* this is very minimum program, just to check if librb is installed
 * on the system or not
 */

#include <rb.h>
#include <stdio.h>

int main(void)
{
    struct rb *rb;

    if ((rb = rb_new(4, 1, 0)) == NULL)
    {
        return 1;
    }

    fprintf(stderr, "rb works! %p\n", rb);
    rb_destroy(rb);
    return 0;
}
