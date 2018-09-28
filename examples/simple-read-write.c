/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ==========================================================================
         -------------------------------------------------------------
        / This example shows the simplest use of librb - that is, put \
        \ some data on it, then read it, then print it                /
         -------------------------------------------------------------
                  \      (__)
                   \     /oo|
                    \   (_"_)*+++++++++*
                           //I#\\\\\\\\I\
                           I[I|I|||||I I `
                           I`I'///'' I I
                           I I       I I
                           ~ ~       ~ ~
                             Scowleton
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
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /*
     * create rb object that can hold 127 (yes, 127) elements each with size
     * of int type.
     */

    if ((rb = rb_new(128, sizeof(int), 0)) == NULL)
    {
        perror("rb_new()");
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
     * don't forget to free memory allocated by rb
     */

    rb_destroy(rb);
    return 0;
}
