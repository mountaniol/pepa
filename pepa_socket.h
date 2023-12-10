#ifndef _PEPA_SOCKET_H__
#define _PEPA_SOCKET_H__

#include <semaphore.h>
#include <pthread.h>

#include "buf_t/buf_t.h"

/* Size of the xross buffer (for reading from / writing to cross socket */
#define X_BUF_SIZE (1024)

typedef struct {
	int fd_src;
	int fd_dst;
	buf_t *buf;
} x_connect_t;

typedef enum {
	X_CONN_COPY_LEFT = 1,
	X_CONN_COPY_RIGHT
} x_conn_direction_t;

typedef struct {
	buf_t * buf_fds; /**< Array of IN sockets file descriptors */
	sem_t buf_fds_mutex; /**< A semaphor used to sync the core struct between multiple threads */
	int buf_changed_flag;
	struct sockaddr_in   s_addr;
	int socket; /** < Socket to accept new connections */
} pepa_in_thread_fds_t;

#endif /* _PEPA_SOCKET_H__ */
