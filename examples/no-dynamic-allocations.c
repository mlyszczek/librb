/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ==========================================================================
         -------------------------------------------------------------
        / This example is very simple to simple-read-write.c, it      \
        | writes to rb, reads to rb, and prints data, but it does not |
        | use any (at least not called directly by any rb functions)  |
        \ dynamic allocation, but local buffer on stack instead.      /
         -------------------------------------------------------------
                \   ^__^
                 \  (oo)\_______
                    (__)\       )\/\
                        ||----w |
                        ||     ||
   ==========================================================================
          _               __            __         ____ _  __
         (_)____   _____ / /__  __ ____/ /___     / __/(_)/ /___   _____
        / // __ \ / ___// // / / // __  // _ \   / /_ / // // _ \ / ___/
       / // / / // /__ / // /_/ // /_/ //  __/  / __// // //  __/(__  )
      /_//_/ /_/ \___//_/ \__,_/ \__,_/ \___/  /_/  /_//_/ \___//____/

   ========================================================================== */


#include "rb.h"

#include <string.h>
#include <stdio.h>


/* ==========================================================================
                                              _
                           ____ ___   ____ _ (_)____
                          / __ `__ \ / __ `// // __ \
                         / / / / / // /_/ // // / / /
                        /_/ /_/ /_/ \__,_//_//_/ /_/

   ========================================================================== */


int main(void)
{
    struct rb  *rb;                  /* pointer to malloced rb object */
    int         i;                   /* simple interator for for loop */
    long        w;                   /* return value from rb_wrote() */
    long        r;                   /* return value from rb_read() */
    int         data_to_write[256];  /* data to write into rb buffer */
    int         data_read[256];      /* buffer where we will read from rb */

    /*
     * allocate memory for rb object and data to hold on stack.
     *
     * This unfortunately will work only on c99 compilers, if  you  want  to
     * use this with c89 compiler, you need to provide static  size  instead
     * of rb_header_size() function.  Since rb .so library file may  change,
     * such code could result in undefined  behaviour.   To  at  least  save
     * yourself from bug huntin you may want to put assert to  see  it  your
     * value is correct:
     *
     * #define HDR_SIZE 32
     * assert(HDR_SIZE >= rb_header_size());
     * unsigned char rb_stack[HDR_SIZE + DATA_SIZE];
     *
     * Last option is to copy 'struct rb'  from  library  .c  file  and  use
     * simple sizeof(), but again, this is recommended only  when  you  know
     * what version of rb object you will be linking  to,  or  else,  struct
     * size in .so file may be different.
     */

    unsigned char  rb_stack[rb_header_size() + 128 * sizeof(int)];
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /*
     * create rb object that can hold 127 (yes, 127) elements each with size
     * of int type, but use provided rb_stack memory  and  DO  NOT  use  any
     * malloc functions.  Note: if you use pthread (pass  O_MULTITHREAD)  to
     * flags, your system may actually allocate some memory when mutexes are
     * initialized.
     */

    if ((rb = rb_init(128, sizeof(int), 0, rb_stack)) == NULL)
    {
        perror("rb_init()");
        return 1;
    }

    /*
     * fill data to send with some data
     */

    for (i = 0; i != rb_array_size(data_to_write); ++i)
    {
        data_to_write[i] = i;
    }

    /*
     * put data in the buffer, buffer can hold only 127 elements, and we try
     * to put 256 bytes there, so rb_write  will  return  127,  as  this  is
     * number of elements copied to rb object.
     */

    w = rb_write(rb, data_to_write, rb_array_size(data_to_write));
    printf("number of elements stored to rb: %ld\n", w);

    /*
     * now we read maximum of 256 elements from rb to data_read buffer,  but
     * since we put 127 elements in rb, only 127  elements  will  be  copied
     * back to data_read
     */

    r = rb_read(rb, data_read, rb_array_size(data_read));
    printf("number of elements read from rb: %ld\n", r);

    /*
     * now let's print what we put through rb, to see it really does work
     */

    for (i = 0; i != r; ++i)
    {
        if (i % 16 == 0)
        {
            printf("\n");
        }

        printf("%02x ", data_read[i]);
    }

    printf("\n");

    /*
     * don't forget to cleanup object when done.
     */

    rb_cleanup(rb);
    return 0;
}
