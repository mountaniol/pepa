#include "pepa_socket.h"
#include "buf_t/buf_t.h"
#include "debug.h"

int pepa_open_socket(struct sockaddr_in *s_addr, buf_t *ip_address, int port)
{
	int sock;

	if (NULL == s_addr) {
		syslog(LOG_ERR, "Can't allocate socket: %s\n", strerror(errno));
		perror("Can't allocate socket");
		return (-1);
	}

	memset(s_addr, 0, sizeof(struct sockaddr_in));
	s_addr->sin_family = (sa_family_t)AF_INET;
	s_addr->sin_port = htons(port);

	if (NULL == ip_address) {
		s_addr->sin_addr.s_addr = htonl(INADDR_ANY);
	} else {
		s_addr->sin_addr.s_addr = htonl(ip_address->arr);
	}

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		syslog(LOG_ERR, "could not create listen socket: %s\n", strerror(errno));
		perror("could not create listen socket");
		goto err1;
	}

	if ((bind(sock, (struct sockaddr *)s_addr, (socklen_t)sizeof(struct sockaddr_in))) < 0) {
		syslog(LOG_ERR, "Can't bind: %s\n", strerror(errno));
		perror("Can't bind");
		goto err1;
	}

	if (listen(sock, SERVER_CLIENTS) < 0) {
		syslog(LOG_ERR, "could not set SERVER_CLIENTS: %s\n", strerror(errno));
		perror("could not set SERVER_CLIENTS");
		goto err1;
	}

	return (sock);

err1:
	return (-1);
}

int pepa_close_shva()
