/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ==========================================================================
         -------------------------------------------------------------
        / This example shows how one could use rb_posix_write to read \
        | network data in a trivial loop, and then process them in    |
        | separate thread in another simple loop. Normally you would  |
        | have to deal with mutexes; and memory buffer overlapping    |
        | and all of that crap. With librb code can be reduced to     |
        \ minimum.                                                    /
         -------------------------------------------------------------
         \     /\  ___  /\
          \   // \/   \/ \\
             ((    O O    ))
              \\ /     \ //
               \/  | |  \/
                |  | |  |
                |  | |  |
                |   o   |
                | |   | |
                |m|   |m|
   ==========================================================================
          _               __            __         ____ _  __
         (_)____   _____ / /__  __ ____/ /___     / __/(_)/ /___   _____
        / // __ \ / ___// // / / // __  // _ \   / /_ / // // _ \ / ___/
       / // / / // /__ / // /_/ // /_/ //  __/  / __// // //  __/(__  )
      /_//_/ /_/ \___//_/ \__,_/ \__,_/ \___/  /_/  /_//_/ \___//____/

   ========================================================================== */



#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "rb.h"


/* ==========================================================================
               ____                     __   _
              / __/__  __ ____   _____ / /_ (_)____   ____   _____
             / /_ / / / // __ \ / ___// __// // __ \ / __ \ / ___/
            / __// /_/ // / / // /__ / /_ / // /_/ // / / /(__  )
           /_/   \__,_//_/ /_/ \___/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


/* ==========================================================================
    Function reads data stored in 'rb' buffer (arg argument)  and  processes
    it and prints to stdout.
   ========================================================================== */


static void *parser(void *arg)
{
    for (;;)
    {
        char d;  /* byte of data read from rb */

        /*
         * read data from rb, byte by byte, process it and print to  stdout.
         * Reading may be faster or slower  depending  on  underlying  mutex
         * implementation, on Linux it will  be  futexes  -  fast  userspace
         * mutexes, and if only one thread will be  calling  rb_read()  then
         * there will be not much system calls, and it will  be  way  faster
         * than calling read() on socket  manually.   Still,  such  approach
         * should rather be used if it increases  overall  code  readability
         * and design, because performance gains (if any) are not conclusive
         */

        if (rb_read(arg, &d, sizeof(d)) != 1)
        {
            if (errno == ECANCELED)
            {
                /*
                 * main thread is going down, not an error, just information
                 * we should not use rb object  anymore,  in  this  case  we
                 * return from the function
                 */

                return NULL;
            }

            perror("rb_read()");
            return NULL;
        }

        if (d == '\n')
        {
            printf("\n");
        }
        else if (d == '\r')
        {
            printf("x%02x(\\r) ", d);
        }
        else
        {
            printf("x%02x(%c) ", d, d);
        }
    }
}


/* ==========================================================================
    Starts server at 127.0.0.1:23893, returns file descriptor for the
    server or calls exit() on failure
   ========================================================================== */


static int start_server(void)
{
    struct sockaddr_in  saddr;  /* address server will be listetning on */
    int                 sfd;    /* server file descriptor */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    memset(&saddr, 0x00, sizeof(saddr));
    saddr.sin_addr.s_addr = htonl(0x7f000001ul); /* 127.0.0.1 */
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(23893);

    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket()");
        exit(1);
    }

    if (bind(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
    {
        perror("bind()");
        exit(1);
    }

    if (listen(sfd, 1) != 0)
    {
        perror("listen()");
        exit(1);
    }

    return sfd;
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
    int         cfd;       /* server file descriptor */
    int         sfd;       /* connected client file descriptor */
    pthread_t   parser_t;  /* thread that parses messages from network */
    struct rb  *rb;        /* pointer to malloced rb object */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /*
     * create rb object that can hold 127 (yes, 127) elements each with size
     * of 1 byte.
     */

    if ((rb = rb_new(128, 1, O_MULTITHREAD)) == NULL)
    {
        perror("rb_new()");
        exit(1);
    }

    sfd = start_server();

    printf("server is up, you can send some messages with following commands\n"
        "to see program in action (not all might be available on your system\n"
        "    1) telnet 127.0.0.1 23893\n"
        "    2) echo \"test message\" | nc -w1 127.0.0.1 23893\n"
        "    3) echo \"test message\" > /dev/tcp/127.0.0.1/23893\n");

    if ((cfd = accept(sfd, NULL, NULL)) < 0)
    {
        perror("accept()");
        exit(1);
    }

    /*
     * create a thread, that will parse data received from network
     */

    pthread_create(&parser_t, NULL, parser, rb);

    for (;;)
    {
        long  w;  /* return value from rb_posix_write() */
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        /*
         * this function will read cfd socket whenever there is something to
         * read from it and all data will be put into rb  object.   Function
         * will block for a long  time  -  it  will  try  to  read  LONG_MAX
         * elements (even if rb is full, it will wait for  someone  to  free
         * some space).  It's nice, as it limits overhead regarding to  some
         * checks.
         */

        w = rb_posix_write(rb, cfd, LONG_MAX);

        if (w == -1)
        {
            /*
             * -1 is bad, there was some bad error - either from select() or
             * read(), you can check errno from these  functions  to  device
             * how to proceed, we simply log error and exits.
             */

            perror("rb_posix_write()");
            break;
        }

        if (w == 0)
        {
            /*
             * this is not an error, in network sockets  it  indicates  that
             * remote client closed connection, so there is no point reading
             * from cfd anymore.
             */

            printf("connection closed by remote client\n");
            break;
        }
    }

    close(cfd);

    /*
     * call rb_stop(), so parser tread can exit from blocked rb_read().
     */

    rb_stop(rb);

    /*
     * it's crucial to wait for any thread locked in rb_* functions after we
     * sent rb_stop() signal to them before we can call rb_destroy()
     */

    pthread_join(parser_t, NULL);

    /*
     * now we are sure no thread is locked in any rb_* functions, so we  can
     * safely destroy the object
     */

    rb_destroy(rb);
    return 0;
}
