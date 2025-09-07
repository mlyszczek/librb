/* ==========================================================================
 *  Licensed under BSD 2clause license. See LICENSE file for more information
 *  Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 * ========================================================================== */
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0);
#define pdie(s) do { perror(s); exit(1); } while (0);

int start_tcp_server(int port);

#endif
