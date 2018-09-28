/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ==========================================================================
         -------------------------------------------------------------
        / This example shows how to use librb as simple event queue.  \
        | Idea is that multiple threads gather different events from  |
        | different sources and pass them to rb objectQ and main      |
        | thread reads them and process them one by one. This is nice |
        \ way to process data asynchronously without polling.         /
         -------------------------------------------------------------
           \
            \
                .--.
               |o_o |
               |:_/ |
              //   \ \
             (|     | )
            /'\_   _/`\
            \___)=(___/
   ==========================================================================
          _               __            __         ____ _  __
         (_)____   _____ / /__  __ ____/ /___     / __/(_)/ /___   _____
        / // __ \ / ___// // / / // __  // _ \   / /_ / // // _ \ / ___/
       / // / / // /__ / // /_/ // /_/ //  __/  / __// // //  __/(__  )
      /_//_/ /_/ \___//_/ \__,_/ \__,_/ \___/  /_/  /_//_/ \___//____/

   ========================================================================== */


#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "rb.h"


/* ==========================================================================
                          __
                         / /_ __  __ ____   ___   _____
                        / __// / / // __ \ / _ \ / ___/
                       / /_ / /_/ // /_/ //  __/(__  )
                       \__/ \__, // .___/ \___//____/
                           /____//_/
   ========================================================================== */


enum event_type
{
    EVENT_KBD,
    EVENT_NET,
    EVENT_SIGNAL
};

struct event
{
    enum event_type  type;
    void            *data;
    long             datalen;
};


/* ==========================================================================
                                   _         __     __
              _   __ ____ _ _____ (_)____ _ / /_   / /___   _____
             | | / // __ `// ___// // __ `// __ \ / // _ \ / ___/
             | |/ // /_/ // /   / // /_/ // /_/ // //  __/(__  )
             |___/ \__,_//_/   /_/ \__,_//_.___//_/ \___//____/

   ========================================================================== */


static struct rb  *queue;  /* pointer to malloced rb object */


/* ==========================================================================
               ____                     __   _
              / __/__  __ ____   _____ / /_ (_)____   ____   _____
             / /_ / / / // __ \ / ___// __// // __ \ / __ \ / ___/
            / __// /_/ // / / // /__ / /_ / // /_/ // / / /(__  )
           /_/   \__,_//_/ /_/ \___/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


/* ==========================================================================
    Waits for keyboard input on stdio, and sends event once new line is read
   ========================================================================== */


static void *kbd_event(void *arg)
{
    (void)arg;

    for (;;)
    {
        char          c;
        int           i;
        struct event  event;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        event.type = EVENT_KBD;
        event.data = malloc(1024);
        i = 0;

        while ((c = getchar()) != '\n')
        {
            *((char *)event.data + i++) = (char)c;

            if (i == 1023)
            {
                /*
                 * overflow imminent, send what we got
                 */

                break;
            }
        }

        *((char *)event.data + i++) = '\0';
        event.datalen = i;

        /*
         * write one event rb object, which is our event queue. Function
         * will return either 1 or -1, when errno is ECANCELED that means
         * rb_stop() has been called and rb object is no longer valid.
         */

        if (rb_write(queue, &event, 1) == -1)
        {
            free(event.data);

            if (errno == ECANCELED)
            {
                return NULL;
            }

            perror("rb_write()");
            return NULL;
        }
    }
}


/* ==========================================================================
    Starts server at 127.0.0.1:23894, and creates net event with data for
    processing
   ========================================================================== */


static void *net_event(void *arg)
{
    struct sockaddr_in  saddr;  /* address server will be listetning on */
    int                 sfd;    /* server file descriptor */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    (void)arg;
    memset(&saddr, 0x00, sizeof(saddr));
    saddr.sin_addr.s_addr = htonl(0x7f000001ul); /* 127.0.0.1 */
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(23894);

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

    for (;;)
    {
        int           cfd;    /* file descriptor of the connected client */
        struct event  event;  /* event to send to event queue */
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        if ((cfd = accept(sfd, NULL, NULL)) < 0)
        {
            perror("accept()");
            exit(1);
        }

        event.type = EVENT_NET;
        event.data = malloc(1024);
        event.datalen = read(cfd, event.data, 1023);

        if (event.datalen == -1)
        {
            perror("read()");
            free(event.data);
            continue;
        }

        *((char *)event.data + event.datalen++) = '\0';

        /*
         * write one event rb object, which is our event queue. Function
         * will return either 1 or -1, when errno is ECANCELED that means
         * rb_stop() has been called and rb object is no longer valid.
         */

        if (rb_write(queue, &event, 1) == -1)
        {
            free(event.data);
            close(cfd);
            close(sfd);

            if (errno == ECANCELED)
            {
                return NULL;
            }

            perror("rb_write()");
            return NULL;
        }
    }
}


/* ==========================================================================
    Signal handler, forwards signal to event queue for processing
   ========================================================================== */



static void sig_event(int signum)
{
    struct event  event;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    /*
     * now signal is interesting, we obviously cannot call  malloc  (another
     * malloc might be in the middle of  call  which  would  leave  us  with
     * corrupted heap), so we cannot use evet.data  to  hold  any  data,  so
     * we'll make a little exception here and  we  put  signum  in  datalen,
     * which may be missleading - but it's safe
     */

    event.datalen = signum;
    event.type = EVENT_SIGNAL;

    /*
     * send event to queue, but make sure to send it with  O_NONBLOCK  here.
     * signals are special, they are like interrupts, and should NEVER block
     * execution
     */

    if (rb_send(queue, &event, 1, O_NONBLOCK) == -1)
    {
        /*
         * queue is full and it's really not wise to block signal, this
         * should be handled somehow, but that's not part of this example,
         * here we simply drop event
         */

        if (errno != EAGAIN)
        {
            perror("rb_send()");
            exit(0);
        }

        return;
    }
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
    pthread_t         kbd_event_t;  /* thread listening for keyboard events */
    pthread_t         net_event_t;  /* thread listening for network events */
    sigset_t          signal_mask;  /* signal mask for newly created threads */
    struct sigaction  sa;           /* sigaction for signal handling */
    long              pid;          /* process id */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /*
     * create rb object that can hold 127 (yes, 127) elements each with size
     * of struct event.
     */

    if ((queue = rb_new(128, sizeof(struct event), O_MULTITHREAD)) == NULL)
    {
        perror("rb_new()");
        exit(1);
    }

    /*
     * create threads that will generate us some events to process and  mask
     * signals, as we want  only  main  thread  to  process  signals,  other
     * functions in threads might use syscalls that signal would  interrupt,
     * we don't want that to happen
     */

    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGINT);
    sigaddset(&signal_mask, SIGTERM);
    sigaddset(&signal_mask, SIGUSR1);
    sigaddset(&signal_mask, SIGUSR2);
    sigaddset(&signal_mask, SIGHUP);
    pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);

    pthread_create(&kbd_event_t, NULL, kbd_event, NULL);
    pthread_create(&net_event_t, NULL, net_event, NULL);

    /*
     * unblock signals and install handler for some of the signals we will
     * be processing.
     */

    pthread_sigmask (SIG_UNBLOCK, &signal_mask, NULL);
    memset(&sa, 0x00, sizeof(sa));
    sa.sa_handler = sig_event;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    pid = (long)getpid();
    printf("program is up, you can send some messages with following commands\n"
        "to see program in action (not all might be available on your system)\n"
        "for network activity do\n"
        "    1) telnet 127.0.0.1 23894\n"
        "    2) echo \"test message\" | nc -w1 127.0.0.1 23894\n"
        "    3) echo \"test message\" > /dev/tcp/127.0.0.1/23894\n"
        "\n"
        "for signal activity do\n"
        "    1) kill -SIGUSR1 %ld\n"
        "    2) kill -SIGUSR2 %ld\n"
        "    3) kill -SIGHUP %ld\n"
        "\n"
        "or you can simply type with you keyboard to stdin and hit enter\n"
        "\n"
        "to interrupt program either send SIGTERM signal or hit ctrl-c\n\n",
        pid, pid, pid);

    for (;;)
    {
        struct event  event;  /* event read from queue */
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        /*
         * read event from the queue, function will  lock  and  sleep  until
         * someone pushes event into the queue
         */

        if (rb_read(queue, &event, 1) == -1)
        {
            perror("rb_read()");
            exit(1);
        }

        switch (event.type)
        {
        case EVENT_KBD:
            printf("---> keyboard event received datalen: %zu, data %s\n",
                event.datalen, (char *)event.data);
            free(event.data);
            break;

        case EVENT_NET:
            printf("---> network event received datalen: %zu, data %s\n",
                event.datalen, (char *)event.data);
            free(event.data);
            break;

        case EVENT_SIGNAL:
            /*
             * this event has one exception, data  -  signal  number  is
             * hold in datalen, instead  of  data  -  check  comment  in
             * sig_event to know why
             */

            printf("---> received signal %s\n", strsignal(event.datalen));

            if (event.datalen == SIGTERM || event.datalen == SIGINT)
            {
                fprintf(stderr, "terminating\n");
                goto stop;
            }

            break;

        default:
            fprintf(stderr, "unexpected event type: %d\n", event.type);
            exit(1);
        }
    }

stop:
    /*
     * meh, too lazy to write proper termination:)
     */

    exit(1);
}
