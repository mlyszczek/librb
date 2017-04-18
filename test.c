/*****************************************************************************
 * Licensed under BSD 3-clause license. See LICENSE file for more information.
 * Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 ****************************************************************************/

#include "rb.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

struct tdata
{
  struct rb *rb;
  unsigned char *data;
  int len;
  int objsize;
  size_t buflen;
};

#ifdef LIBRB_PTHREAD
static void *consumer(void *arg)
{
  struct tdata *data = arg;
  size_t read = 0;

  while (read != data->buflen)
    {
      read += rb_read(data->rb, data->data + read * data->objsize, data->len);
    }

  return data;
}

static void *producer(void *arg)
{
  struct tdata *data = arg;
  size_t written = 0;

  while (written != data->buflen)
    {
      written += rb_write(data->rb, data->data + written * data->objsize,
                          data->len);
    }

  return data;
}

static void multi_thread(int rblen, int readlen, int writelen, int objsize)
{
  pthread_t cons;
  pthread_t prod;

  size_t buflen = readlen > writelen ? readlen : writelen;
  unsigned char *send_buf = malloc(objsize * buflen);
  unsigned char *recv_buf = malloc(objsize * buflen);
  size_t i;

  struct rb rb;
  struct tdata consdata;
  struct tdata proddata;
  static unsigned long c;
  c++;

  for (i = 0; i != objsize * buflen; ++i)
    {
      send_buf[i] = i;
      recv_buf[i] = 0;
    }

  rb_new(&rb, rblen, objsize, 0);

  proddata.data = send_buf;
  proddata.len = writelen;
  proddata.objsize = objsize;
  proddata.rb = &rb;
  proddata.buflen = buflen;

  consdata.data = recv_buf;
  consdata.len = readlen;
  consdata.objsize = objsize;
  consdata.rb = &rb;
  consdata.buflen = buflen;

  pthread_create(&cons, NULL, consumer, &consdata);
  pthread_create(&prod, NULL, producer, &proddata);

  pthread_join(cons, NULL);
  pthread_join(prod, NULL);

  if (memcmp(send_buf, recv_buf, objsize * buflen) != 0)
    {
      printf("[NOK]\n");
      printf("[%lu] a = %lu, b = %d, c = %d, d = %d\n",
             c, buflen, rblen, readlen, writelen);
    };

  rb_destroy(&rb);
  free(send_buf);
  free(recv_buf);
}
#endif

static void single_thread(int rblen, int readlen, int writelen, int objsize)
{
  size_t read;
  size_t written;
  size_t buflen = readlen > writelen ? readlen : writelen;

  unsigned char *send_buf = malloc(objsize * buflen);
  unsigned char *recv_buf = malloc(objsize * buflen);

  struct rb rb;
  static unsigned long c;
  int flags;
  size_t i;

  c++;

  for (i = 0; i != objsize * buflen; ++i)
    {
      send_buf[i] = i;
      recv_buf[i] = 0;
    }

  flags = 0;
#ifdef LIBRB_PTHREAD
  flags = O_NONBLOCK | O_NONTHREAD;
#endif

  rb_new(&rb, rblen, objsize, flags);

  written = 0;
  read = 0;

  while (written != buflen || read != buflen)
    {
      if (written != buflen)
        {
          if (written + writelen > buflen)
            {
              writelen = buflen - written;
            }

          written += rb_write(&rb, send_buf + written * objsize, writelen);
        }

      if (read != buflen)
        {
          if (read + readlen > buflen)
            {
              readlen = buflen - read;
            }

          read+= rb_read(&rb, recv_buf + read * objsize, readlen);
        }
    }

  if (memcmp(send_buf, recv_buf, buflen * objsize) != 0)
    {
      printf("[NOK]\n");
      printf("[%lu] a = %lu, b = %d, c = %d, d = %d\n",
              c, buflen, rblen, readlen, writelen);
    }

  free(send_buf);
  free(recv_buf);
  rb_destroy(&rb);
}

int main(void)
{
  unsigned long a,b,c,d;
  unsigned long al, bl, cl, dl;

  al = 1024;
  bl = 1024;
  cl = 1024;
  dl = 1024;

  printf("runnin tests\n");

  for (a = 2; a != al; a *= 2)
    {
      for (b = 2; b != bl; b *= 2)
        {
          for (c = 2; c != cl; c *= 2)
            {
              for (d = 2; d != dl; d *= 2)
                {
#ifdef LIBRB_PTHREAD
                  multi_thread(a,b,c,d);
#endif
                  single_thread(a,b,c,d);
                }
            }
        }
    }

  printf("test done. If this is the only line, there were no errors\n");
  return 0;
}
