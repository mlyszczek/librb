/* ==========================================================================
 *  Licensed under BSD 2clause license See LICENSE file for more information
 *  Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 * ========================================================================== */

#include <string.h>
#include <stdio.h>

#include "rb.h"
#include "common.h"

void scatter_read(struct rb *rb)
{
   char str[] = "test\0data\0to\0read\n\0";
   char a[5], b[5], c[3], d[16];
   struct rb_iovec iov[] = {
       { .base = a, .len = sizeof(a) },
       { .base = b, .len = sizeof(b) },
       { .base = c, .len = sizeof(c) },
       { .base = d, .len = sizeof(d) },
    };

    rb_write(rb, str, sizeof(str));
    rb_readv(rb, iov, rb_array_size(iov));
    printf("%s %s %s %s", a, b, c, d);
    /* will print "test data to read" */
}

void gather_write(struct rb *rb)
{
   char rdbuf[32];
   char a[] = "test ";
   char b[] = "data ";
   char c[] = "to ";
   char d[] = "write\n";
   struct rb_iovec iov[] = {
       { .base = a, .len = strlen(a) },
       { .base = b, .len = strlen(b) },
       { .base = c, .len = strlen(c) },
       { .base = d, .len = strlen(d) },
    };

    rb_writev(rb, iov, rb_array_size(iov));
    rb_read(rb, rdbuf, sizeof(rdbuf));
    printf("%s", rdbuf);
    /* will print "test data to write" */
}

int main(void)
{
	struct rb *rb = rb_new(128, 2, rb_dynamic);
	scatter_read(rb);
	gather_write(rb);
	rb_destroy(rb);
	return 0;
}
