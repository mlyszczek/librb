/* ==========================================================================
 *  Licensed under BSD 2clause license See LICENSE file for more information
 *  Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
 * ========================================================================== */

#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>

#include "common.h"

/** =========================================================================
 *  Starts server at 127.0.0.1:#port, returns file descriptor for the
 *  server or calls exit() on failure
 *
 * @return file descriptor for the server socket
 * @return -1 on errors
 * ========================================================================== */
int start_tcp_server(int port)
{
	struct sockaddr_in  saddr;  /* address server will be listening on */
	int                 sfd;    /* server file descriptor */
	int                 yes = 1;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	memset(&saddr, 0x00, sizeof(saddr));
	saddr.sin_addr.s_addr = htonl(0x7f000001ul); /* 127.0.0.1 */
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);

	if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		pdie("socket()");
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)))
		pdie("setsockopt(SO_REUSEADDR)");
	if (bind(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
		pdie("bind()");
	if (listen(sfd, 1) != 0)
		pdie("listen()");

	return sfd;
}
