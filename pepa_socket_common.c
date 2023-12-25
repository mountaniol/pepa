#define _GNU_SOURCE
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>

#include "slog/src/slog.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"

#define RX_TX_PRINT_DIVIDER (50000)

#define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

void set_sig_handler(void)
{
	sigset_t set;
	sigfillset(&set);

	int sets = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if (sets != 0) {
		handle_error_en(sets, "pthread_sigmask");
		exit(-1);
	}
}

int pepa_pthread_init_phase(const char *name)
{
	slog_note("####################################################");
	slog_note("Thread %s: Detaching", name);
	if (0 != pthread_detach(pthread_self())) {
		slog_fatal("Thread %s: can't detach myself", name);
		perror("Thread : can't detach myself");
		return -PEPA_ERR_THREAD_DETOUCH;
	}

	slog_note("Thread %s: Detached", name);
	slog_note("####################################################");

	int rc = pthread_setname_np(pthread_self(), name);
	if (0 != rc) {
		slog_fatal("Thread %s: can't set name", name);
	}

	return PEPA_ERR_OK;
}

void pepa_parse_pthread_create_error(const int rc)
{
	switch (rc) {
	case EAGAIN:
		slog_fatal("Insufficient resources to create another thread");
		break;
	case EINVAL:
		slog_fatal("Invalid settings in attr");
		break;
	case EPERM:
		slog_fatal("No permission to set the scheduling policy and parameters specified in attr");
		break;
	default:
		slog_fatal("You should never see this message: error code %d", rc);
	}
}

pepa_fds_t *pepa_fds_t_alloc(int fd_read,
							 int fd_write,
							 int close_read_sock,
							 sem_t *out_mutex,
							 char *name_thread,
							 char *name_read,
							 char *name_write)
{
	pepa_fds_t *fdx = malloc(sizeof(pepa_fds_t));
	if (NULL == fdx) {
		slog_fatal("Can't allocate");
		return (NULL);
	}

	memset(fdx, 0, sizeof(pepa_fds_t));

	fdx->fd_read = fd_read;
	fdx->fd_write = fd_write;
	fdx->close_read_sock = close_read_sock;
	fdx->fd_write_mutex = out_mutex;
	fdx->name = name_thread;
	fdx->name_read = name_read;
	fdx->name_write = name_write;
	return (fdx);
}

void pepa_fds_t_release(pepa_fds_t *fdx)
{
	if (NULL == fdx) {
		slog_fatal("Arg is NULL");
		return;
	}

	/* Do not touch the semaphore, this strcture doesn't own it */
	/* Secure way: clear memory before release it */
	memset(fdx, 0, sizeof(pepa_fds_t));
	free(fdx);
}

uint32_t pepa_thread_counter(void)
{
	static uint32_t counter = 0;
	return ++counter;
}

int pepa_one_direction_copy2(int fd_out, const char *name_out,
							 int fd_in, const char *name_in,
							 char *buf, size_t buf_size, int do_debug)
{
	int ret       = PEPA_ERR_OK;
	int rx        = 0;
	int tx_total  = 0;
	int rx_total  = 0;
	int iteration = 0;

	if (do_debug) {
		DDD0("Starrting transfering from %s to %s", name_in, name_out);
	}

	do {
		int tx         = 0;
		int tx_current = 0;

		iteration++;
		if (do_debug) {
			DDD0("Iteration: %d", iteration);
		}

		/* Read one time, then we will transfer it possibly in pieces */
		rx = read(fd_in, buf, buf_size);

		if (do_debug) {
			DDD0("Iteration: %d, finised read(), there is %d bytes", iteration, rx);
		}

		if (rx < 0) {
			if (do_debug) {
				slog_error("Could not read: from read sock %s [%d]: %s", name_in, fd_in, strerror(errno));
			}
			ret = -PEPA_ERR_BAD_SOCKET_READ;
			goto endit;
		}

		/* nothing to read*/
		if ((0 == rx) && (1 == iteration)) {
			/* If we can not read on the first iteration, it probably means the fd was closed */
			ret = -PEPA_ERR_BAD_SOCKET_READ;

			if (do_debug) {
				slog_error("Could not read on the first iteration: from read sock %s [%d]: %s", name_in, fd_in, strerror(errno));
			}

		}

		if (PEPA_ERR_OK != ret) {
			goto endit;
		}


		/* Write until transfer the whole received buffer */
		do {
			tx_current  = write(fd_out, buf, (rx - tx));

			if (tx_current < 0) {
				ret = -PEPA_ERR_BAD_SOCKET_WRITE;
				if (do_debug) {
					slog_warn("Could not write to write to sock %s [%d]: returned -1: %s", name_out, fd_out, strerror(errno));
				}
				goto endit;
			}

			tx += tx_current;

			/* If we still not transfered everything, give the TX socket some time to rest and finish the transfer */
			if (tx < rx) {
				usleep(10);
			}

		} while (tx < rx);

		rx_total += rx;
		tx_total += tx;
		if (do_debug) {
			DDD0("Iteration %d done: rx = %d, tx = %d", iteration, rx, tx);
		}
		//} while (rx > 0); /* Tun this loop as long as we have data on read socket */

	} while ((int)buf_size == rx); /* Tun this loop as long as we have data on read socket */

	ret = PEPA_ERR_OK;
endit:
	if (do_debug) {
		DDD0("Finished transfering from %s to %s, returning %d, rx = %d, tx = %d, ", name_in, name_out, ret, rx_total, tx_total);
	}
	return ret;
}


#if 0 /* SEB */
int pepa_one_direction_copy(pepa_fds_t *fdx, buf_t *buf){
	int ret       = PEPA_ERR_OK;
	int rx        = 0;
	int tx_total  = 0;
	int rx_total  = 0;
	int iteration = 0;

	do {
		int tx         = 0;
		int tx_current = 0;

		iteration++;

		DDD0("Going to read from %s fd %d to buf size %ld", fdx->name_read, fdx->fd_read, buf->room);

		/* Read one time, then we will transfer it possibly in pieces */
		rx = read(fdx->fd_read, buf->data, buf->room);
		fdx->reads++;

		if (rx < 0) {
			slog_error("Could not read: from %s %s", fdx->name_read, strerror(errno));
			ret = -PEPA_ERR_BAD_SOCKET_READ;
			goto endit;
		}

		/* nothing to read*/
		if (0 == rx) {
			/* If we can not read on the first iteration, it probably means the fd was closed */
			if (1 == iteration) {
				ret = -PEPA_ERR_BAD_SOCKET_READ;
				// ret = PEPA_ERR_OK;
			} else {
				ret = PEPA_ERR_OK;
			}

			//DD0("Read descriptor is empty");
			goto endit;
		}

		DDD0("Going to write to %s fd %d from buf size %d, used %d",
			 fdx->name_write,
			 fdx->fd_read,
			 rx, rx);

		/* Write until transfer the whole received buffer */
		do {
			if (fdx->fd_write_mutex) {
				sem_wait(fdx->fd_write_mutex);
			}

			tx_current  = write(fdx->fd_write, (buf->data + tx), (rx - tx));
			fdx->writes++;

			if (fdx->fd_write_mutex) {
				sem_post(fdx->fd_write_mutex);
			}

			if (tx_current < 0) {
				ret = -PEPA_ERR_BAD_SOCKET_WRITE;
				slog_warn("Could not write to %s: returned -1: %s", fdx->name_write, strerror(errno));
				goto endit;
			}

			tx += tx_current;

			/* If we still not transfered everything, give the TX socket some time to rest and finish the transfer */
			if (tx < rx) {
				usleep(10);
			}

		} while (tx < rx);

		rx_total += rx;
		tx_total += tx;
	} while (rx > 0); /* Tun this loop as long as we have data on read socket */

	ret = PEPA_ERR_OK;
	endit:
	fdx->rx += rx_total;
	fdx->tx += tx_total;
	#if 0 /* SEB */
	if (written_acc + rd_acc > 0) {
		slog_note("%s -> %s written_acc %d, rd_acc = %d", fdx->name_read, fdx->name_write, written_acc, rd_acc);
	}
	#endif
	return ret;
}
#endif

int pepa_test_fd(int fd)
{
	if ((fcntl(fd, F_GETFL) < 0) && (EBADF == errno)) {
		//slog_error("File descriptor %d is invalid: %s", fd, strerror(errno));
		return -PEPA_ERR_FILE_DESCRIPTOR;
	}
	return PEPA_ERR_OK;
}

int epoll_ctl_add(int epfd, int fd, uint32_t events)
{
	struct epoll_event ev;

	if (fd < 0) {
		slog_fatal("Tryed to add fd < 0: %d", fd);
		return -PEPA_ERR_FILE_DESCRIPTOR;
	}

	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		// if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		slog_fatal("Can not add fd %d to epoll: %s", fd, strerror(errno));
		// perror("epoll_ctl()");
		//exit(1);
		return -PEPA_ERR_EPOLL_CANNOT_ADD;
	}

	return PEPA_ERR_OK;
}

#if 0 /* SEB */
static void pepa_one_direction_rw_thread_cleanup(void *arg){
	pepa_fds_t *fdx        = (pepa_fds_t *)arg;
	slog_note("Running RW THREAD EXIT [%s] cleanup routine", fdx->my_name);

	/* We should close the read file descriptor if the flag 'close reading socket' is set */
	if (fdx->close_read_sock) {
		int close_rc = close(fdx->fd_read);
		if (0 != close_rc) {
			slog_error("%s: Could not close reading socket: %s", fdx->my_name, strerror(errno));
		}
	}

	if (fdx->buf) {
		int rc = buf_free(fdx->buf);
		if (BUFT_OK != rc) {
			slog_error("Could not release buf_t");
		}
	}

	if (fdx->fd_eventpoll) {
		close(fdx->fd_eventpoll);
	}

	slog_debug("***** %-14s TRANSFER THREAD ENDED: TRANSFERED: reads: %-5lu, writes: %-5lurx bytes: %-10lu Kb:%-8lu tx: bytes: %-10lu Kb:%-8lu *****",
	   fdx->my_name, fdx->reads, fdx->writes, fdx->rx, fdx->rx / 1024, fdx->tx, fdx->tx / 1024);

	/* Release struct */
	pepa_fds_t_release(fdx);
}
#endif

#if 0 /* SEB */
void *pepa_one_direction_rw_thread(void *arg){
	int        i;
	int        event_count;
	buf_t      *buf        = buf_new(1024);
	int        rc;
	pepa_fds_t *fdx        = (pepa_fds_t *)arg;
	fdx->buf = buf;

	//char       my_name[32] = {0};
	snprintf(fdx->my_name, 32, "<CP-%s-%d>", fdx->name, pepa_thread_counter());

	/* Push cleanup structure */
	pthread_cleanup_push(pepa_one_direction_rw_thread_cleanup, arg);

	/* Init the pthread  */
	rc = pepa_pthread_init_phase(fdx->my_name);
	if (rc < 0) {
		slog_error("%s:Could not init CTL", fdx->my_name);
		pthread_exit(NULL);
	}

	slog_note("%s: Starting: %s -> %s, fd read: %d, fd_write: %d",
		fdx->my_name, fdx->name_read, fdx->name_write, fdx->fd_read, fdx->fd_write);

	if (fdx->fd_read < 0 || fdx->fd_write < 0) {
		slog_error("%s: One of descriptors is invalid", fdx->my_name);
		pthread_exit(NULL);
	}

	/* EPLOLL */

	struct epoll_event events[2];
	int                epoll_fd  = epoll_create1(EPOLL_CLOEXEC);

	if (0 != epoll_ctl_add(epoll_fd, fdx->fd_read, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		slog_warn("%s: Tried to add fdx->fd_read = %d and failed", fdx->my_name, fdx->fd_read);
		pthread_exit(NULL);
	}

	fdx->fd_eventpoll = epoll_fd;

	if (epoll_ctl_add(epoll_fd, fdx->fd_write, EPOLLOUT | EPOLLRDHUP | EPOLLHUP)) {
		//if (epoll_ctl_add(epoll_fd, fdx->fd_write, EPOLLRDHUP | EPOLLHUP)) {
		slog_warn("%s: Tried to add fdx->fd_write = %d and failed", fdx->my_name,  fdx->fd_write);
		pthread_exit(NULL);
	}

	do {
		event_count = epoll_wait(epoll_fd, events, 1, 300000);
		int err = errno;
		/* Nothing to do, exited by timeout */
		if (0 == event_count) {
			continue;
		}

		/* Interrupted by a signal */
		if (event_count < 0 && EINTR == err) {
			continue;
		}

		if (event_count < 0) {
			slog_fatal("%s: error on wait: %s", fdx->my_name, strerror(err));
			pthread_exit(NULL);
		}

		for (i = 0; i < event_count; i++) {

			/* If one of the read/write sockets is diconnected, exit the thread */
			if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
				slog_warn("%s: Connection |%s = %d| is closed, terminate thread: %s", fdx->my_name,
					 (events[i].data.fd == fdx->fd_read) ? "read" : "write",
					 (events[i].data.fd == fdx->fd_read) ? fdx->fd_read : fdx->fd_write,
					 strerror(err));
				pthread_exit(NULL);
			}

			/* Read from socket */
			if ((events[i].data.fd == fdx->fd_read) && (events[i].events & EPOLLIN)) {
				rc = pepa_one_direction_copy(fdx, buf);
				if (rc < 0) {
					slog_warn("%s: Read/Write op between sockets failure: fd read: %d. fd out: %d",
						fdx->my_name, fdx->fd_read, fdx->fd_write);
					//pepa_event_send(fdx->fd_event, 1);
					pthread_exit(NULL);
				}
				continue;

			} /* End of read descriptor processing */

			/* The write descriptor is ready for write; we ignore it */
			if (events[i].data.fd == fdx->fd_write) {
				continue;
			}
		}
	#if 1 /* SEB */
		/* Test all descriptors, and exit thread if one of them is invalid */
		if (0 != pepa_test_fd(fdx->fd_read)) {
			slog_warn("%s: read fd is invalid, terminate", fdx->my_name);
			pthread_exit(NULL);
		}

		if (0 != pepa_test_fd(fdx->fd_write)) {
			slog_warn("%s: write fd is invalid, terminate", fdx->my_name);
			pthread_exit(NULL);
		}
	#endif

		if ((fdx->reads > 0) && (0 == (fdx->reads % RX_TX_PRINT_DIVIDER))) {
			slog_debug("===== %-14s TRANSFER THREAD WORKING: TRANSFERED: reads: %-7lu writes: %-7lurx bytes: %-10lu Kb:%-8lu tx: bytes: %-10lu Kb:%-8lu =====",
			   fdx->my_name, fdx->reads, fdx->writes, fdx->rx, fdx->rx / 1024, fdx->tx, fdx->tx / 1024);
		}
	} while (1);


	pthread_cleanup_pop(0);
	return NULL;
}
#endif

int pepa_socket_shutdown_and_close(int sock, const char *my_name)
{
	if (sock < 0) {
		return -PEPA_ERR_FILE_DESCRIPTOR;
	}

	int rc = shutdown(sock, SHUT_RDWR);
	if (rc < 0) {
		slog_warn("%s: Could not shutdown the socket: fd: %d, %s", my_name, sock, strerror(errno));
		return -PEPA_ERR_FILE_DESCRIPTOR;
	}

	rc = close(sock);
	if (rc < 0) {
		slog_warn("%s: Could not close the socket: fd: %d, %s", my_name, sock, strerror(errno));
		return -PEPA_ERR_CANNOT_CLOSE;
	}

	close(sock);
	slog_note("%s: Closed socket %d", my_name, sock);
	return PEPA_ERR_OK;
}

__attribute__((nonnull(1, 2)))
int pepa_open_listening_socket(struct sockaddr_in *s_addr, const buf_t *ip_address, const int port, const int num_of_clients, const char *name)
{
	int rc   = PEPA_ERR_OK;
	int sock;

	slog_debug("Open Socket [from %s]: starting for %s:%d, num of clients: %d",
	   name, ip_address->data, port, num_of_clients);

	memset(s_addr, 0, sizeof(struct sockaddr_in));
	s_addr->sin_family = (sa_family_t)AF_INET;
	s_addr->sin_port = htons(port);

	slog_debug("Open Socket [from %s]: Port is %d", name, port);

	const int inet_aton_rc = inet_aton(ip_address->data, &s_addr->sin_addr);

	slog_note("Open Socket [from %s]: specified address, use %s", name, ip_address->data);
	/* Warning: inet_aton() returns 1 on success */
	if (1 != inet_aton_rc) {
		slog_fatal("Open Socket [from %s]: Could not convert string address to in_addr_t: %s", name, strerror(errno));
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	DDD0("Open Socket [from %s]: Going to create socket for %s:%d", name, ip_address->data, port);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock  < 0) {
		slog_error("Open Socket [from %s]: Could not create listen socket: %s", name, strerror(errno));
		//perror("could not create listen socket");
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	const int enable = 1;
	rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (rc < 0) {
		slog_error("Open Socket [from %s]: Could not set SO_REUSEADDR on socket, error: %s", name, strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}

#if 0 /* SEB */
	rc = setsockopt(sock, SOL_SOCKET, MSG_NOSIGNAL, (void *)&enable, sizeof(int));
	if (rc < 0) {
		slog_error("Open Socket [from %s]: Could not set SO_NOSIGPIPE on socket, error: %s", name, strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}
#endif

	do {
		rc = bind(sock, (struct sockaddr *)s_addr, (socklen_t)sizeof(struct sockaddr_in));
		if (rc < 0 && EADDRINUSE == errno) {
			slog_warn("Address in use, can't bind, [from %s], waiting...", name);
			sleep(10);
		}
	} while (rc < 0 && EADDRINUSE == errno);

	if (rc < 0) {
		//perror("Can't bind: ");
		slog_error("Open Socket [from %s]: Can't bind: %s", name, strerror(errno));
		close(sock);
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_IN_USE);
	}

	rc = listen(sock, num_of_clients);
	if (rc < 0) {
		slog_fatal("Open Socket [from %s]: Could not set SERVER_CLIENTS: %s", name, strerror(errno));
		//perror("could not set SERVER_CLIENTS");
		close(sock);
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_LISTEN);
	}

	slog_note("Open Socket [from %s]: Opened listening socket: %d", name, sock);
	return (sock);
}

int pepa_open_connection_to_server(const char *address, int port, const char *name)
{
	struct sockaddr_in s_addr;
	int                sock;

	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = (sa_family_t)AF_INET;

	const int convert_rc = inet_pton(AF_INET, address, &s_addr.sin_addr);
	if (0 == convert_rc) {
		slog_fatal("[from %s]: The string is not a valid IP address: |%s|", name, address);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	if (convert_rc < 0) {
		slog_fatal("[from %s]: Could not convert string addredd |%s| to binary", name, address);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	s_addr.sin_port = htons(port);

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		slog_fatal("[from %s]:: Could not create socket", name);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_CREATION);
	}

#if 0 /* SEB */
	const int enable = 1;
	int rc = setsockopt(sock, SOL_SOCKET, MSG_NOSIGNAL, (void *)&enable, sizeof(int));
	if (rc < 0) {
		slog_error("Open Socket [from %s]: Could not set SO_NOSIGPIPE on socket, error: %s", name, strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}
#endif


	if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
		slog_error("[from %s]: Could not connect to server: %s", name, strerror(errno));
		close(sock);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCK_CONNECT);
	}

	slog_info("[from %s]: Opened connection to server: %d", name, sock);

	return (sock);
}



