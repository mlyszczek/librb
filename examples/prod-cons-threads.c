/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ==========================================================================
         ------------------------------------------------------------
        / This example uses threads. It is classic consumer-producer \
        | problem that thanks to rb internal thread awarness is      |
        | reduced to minimum. Note that there is no mutex in this    |
        | file, all synchronization is performed by rb. Program      |
        | prints blocks representing count elements in rb to         |
        \ visualise how elements are popped and pushed               /
         ------------------------------------------------------------
             \
              \
                  oO)-.                       .-(Oo
                 /__  _\                     /_  __\
                 \  \(  |     ()~()         |  )/  /
                  \__|\ |    (-___-)        | /|__/
                  '  '--'    ==`-'==        '--'  '
   ==========================================================================
          _               __            __         ____ _  __
         (_)____   _____ / /__  __ ____/ /___     / __/(_)/ /___   _____
        / // __ \ / ___// // / / // __  // _ \   / /_ / // // _ \ / ___/
       / // / / // /__ / // /_/ // /_/ //  __/  / __// // //  __/(__  )
      /_//_/ /_/ \___//_/ \__,_/ \__,_/ \___/  /_/  /_//_/ \___//____/

   ========================================================================== */


#include "rb.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>


/* ==========================================================================
                                   _         __     __
              _   __ ____ _ _____ (_)____ _ / /_   / /___   _____
             | | / // __ `// ___// // __ `// __ \ / // _ \ / ___/
             | |/ // /_/ // /   / // /_/ // /_/ // //  __/(__  )
             |___/ \__,_//_/   /_/ \__,_//_.___//_/ \___//____/

   ========================================================================== */


static struct rb  *rb;  /* pointer to malloced rb object */
static const char *bar = "===================================================";


/* ==========================================================================
               ____                     __   _
              / __/__  __ ____   _____ / /_ (_)____   ____   _____
             / /_ / / / // __ \ / ___// __// // __ \ / __ \ / ___/
            / __// /_/ // / / // /__ / /_ / // /_/ // / / /(__  )
           /_/   \__,_//_/ /_/ \___/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


static void *consumer(void *arg)
{
    struct timespec  req;
    int              sleep_ms;
    int              i;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    sleep_ms = *(int *)arg;
    req.tv_sec = sleep_ms / 1000;
    req.tv_nsec = 1000000L * (sleep_ms % 1000);

    for (i = 0; i != 15; ++i)
    {
        int data;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        if (rb_read(rb, &data, 1) != 1)
        {
            perror("rb_read()");
            exit(1);
        }

        printf("<--  %.*s\n", (int)rb_count(rb), bar);
        nanosleep(&req, NULL);
    }

    return NULL;
}


static void *producer(void *arg)
{
    struct timespec  req;
    int              sleep_ms;
    int              i;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    sleep_ms = *(int *)arg;
    req.tv_sec = sleep_ms / 1000;
    req.tv_nsec = 1000000L * (sleep_ms % 1000);

    for (i = 0; i != 15; ++i)
    {
        if (rb_write(rb, &i, 1) != 1)
        {
            perror("rb_write()");
            exit(1);
        }

        printf("-->  %.*s\n", (int)rb_count(rb), bar);
        nanosleep(&req, NULL);
    }

    return NULL;
}


/* ==========================================================================
                                              _
                           ____ ___   ____ _ (_)____
                          / __ `__ \ / __ `// // __ \
                         / / / / / // /_/ // // / / /
                        /_/ /_/ /_/ \__,_//_//_/ /_/

   ========================================================================== */


int main(void)
{
    int         cons_sleep;
    int         prod_sleep;
    pthread_t   cons_t;
    pthread_t   prod_t;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /*
     * create rb object that can hold 7 (yes, 7) elements each with size  of
     * int type.
     */

    if ((rb = rb_new(8, sizeof(int), O_MULTITHREAD)) == NULL)
    {
        perror("rb_new()");
        return 1;
    }

    /*
     * now start threads, one will produce data, and one will consume  them.
     * In first round, consumer will be consuming data faster than  producer
     * can produce them, you will see that consumer will take a lot of  data
     * from rb quickly, and then it will see rb is empty,  and  will  go  to
     * sleep until prod produces data.
     */

    cons_sleep = 100;
    prod_sleep = 500;

    printf("=====] consumer delay: 100ms, producer delay: 1000ms [=====\n");
    printf("-----| spawning producer |-----\n");
    pthread_create(&prod_t, NULL, producer, &prod_sleep);
    sleep(5);
    printf("-----| spawning consumer |-----\n");
    pthread_create(&cons_t, NULL, consumer, &cons_sleep);

    pthread_join(cons_t, NULL);
    pthread_join(prod_t, NULL);

    /*
     * now producer will produce data faster than consumer can consume them.
     * You will see that producer produces a lot of data  quickly  and  then
     * will block until consumer take some data and make space in buffer
     */

    cons_sleep = 500;
    prod_sleep = 100;

    printf("=====] consumer delay: 1000ms, producer delay: 100ms [=====\n");
    printf("-----| spawning producer |-----\n");
    pthread_create(&prod_t, NULL, producer, &prod_sleep);
    printf("-----| spawning consumer |-----\n");
    pthread_create(&cons_t, NULL, consumer, &cons_sleep);

    pthread_join(cons_t, NULL);
    pthread_join(prod_t, NULL);

    /*
     * don't forget to free memory allocated by rb
     */

    rb_destroy(rb);
    return 0;
}
