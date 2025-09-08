/* ==========================================================================
 *  Licensed under BSD 2clause license See LICENSE file for more information
 *  Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 * ========================================================================== */
#include <string.h>
#include <stdio.h>

#include "rb.h"
#include "common.h"

#define STACK_ALLOCATION 1

int main(void)
{
#if STACK_ALLOCATION
	struct rb   rbs;                 /* stack allocated ring buffer object */
	int         buffer[128];         /* buffer to hold 128 integers */
#endif
	struct rb  *rb;                  /* pointer to new rb object */
	long        nwritten;            /* return value from rb_write() */
	long        nread;               /* return value from rb_read() */
	int         data_to_write[256];  /* data to write into rb buffer */
	int         data_read[256];      /* buffer where we will read from rb */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	/* You can use stack or heap allocations. Pick your poison. */
#if STACK_ALLOCATION
	/* Initialize stack allocated #rb. Use stack allocated #buffer to
	 * hold data. Tell #rb array properties that is length and single
	 * element size */
	if (rb_init(&rbs, buffer, rb_array_size(buffer), sizeof(int), 0))
		pdie("rb_init()");
	rb = &rbs;
#else
	/* Initialize ring buffer using heap. Buffer of #rb it allocated
	 * internally as well */
	if ((rb = rb_new(128, sizeof(int), 0)) == NULL)
		pdie("rb_init()");
#endif

	/* fill data to send with some data */
	for (int i = 0; i != rb_array_size(data_to_write); ++i)
		data_to_write[i] = i;

	/* put data in the buffer, buffer can hold only 127 elements, and we try
	 * to put 256 integers (elements) there, so rb_write will return 127,
	 * as this is number of elements copied to rb object. */
	nwritten = rb_write(rb, data_to_write, rb_array_size(data_to_write));
	printf("number of elements stored to rb: %ld\n", nwritten);

	/* buffer is now full, any write to it will result in error */
	if (rb_write(rb, data_read, 1) == -1)
		perror("rb_write() returned error");

	/* now we read maximum of 256 elements from rb to data_read buffer, but
	 * since we put 127 elements in rb, only 127 elements will be copied
	 * back to #data_read */
	memset(data_read, 0x00, sizeof(data_read));
	nread = rb_read(rb, data_read, rb_array_size(data_read));
	printf("number of elements read from rb: %ld\n", nread);

	/* buffer is now empty, any read from it will result in error */
	if (rb_read(rb, data_read, 1) == -1)
		perror("rb_write() returned error");

	/* check if data read matches what we've just put on buffer */
	printf("Checking if data matches... data %s\n",
		memcmp(data_read, data_to_write, nread * sizeof(int)) ? "not ok" : "ok");

	/* don't forget to cleanup object when done. */
#if STACK_ALLOCATION
	rb_cleanup(rb);
#else
	rb_destroy(rb);
#endif

	return 0;
}
