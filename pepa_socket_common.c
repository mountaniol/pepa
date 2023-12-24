#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h> /* For read() */
#include <sys/eventfd.h> /* For eventfd */
/* THe next two are needed for send() */
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>

#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_debug.h"
#include "pepa_state_machine.h"
#include "buf_t/buf_t.h"
#include "buf_t/se_debug.h"

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
	DDD("####################################################\n");
	DDD("Thread %s: Detaching\n", name);
	if (0 != pthread_detach(pthread_self())) {
		DE("Thread %s: can't detach myself\n", name);
		perror("Thread : can't detach myself");
		return -1;
	}

	DDD("Thread %s: Detached\n", name);
	DDD("####################################################\n");

	int rc = pthread_setname_np(pthread_self(), name);
	if (0 != rc) {
		DE("Thread %s: can't set name\n", name);
	}

	return PEPA_ERR_OK;
}

void pepa_parse_pthread_create_error(const int rc)
{
	switch (rc) {
	case EAGAIN:
		DE("Insufficient resources to create another thread\n");
		break;
	case EINVAL:
		DE("Invalid settings in attr\n");
		break;
	case EPERM:
		DE("No permission to set the scheduling policy and parameters specified in attr\n");
		break;
	default:
		DE("You should never see this message: error code %d\n", rc);
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
		DE("Can't allocate\n");
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
		DE("Arg is NULL\n");
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
		DDD0("Starrting transfering from %s to %s\n", name_in, name_out);
	}

	do {
		int tx         = 0;
		int tx_current = 0;

		iteration++;
		if (do_debug) {
			DDD0("Iteration: %d\n", iteration);
		}

		/* Read one time, then we will transfer it possibly in pieces */
		rx = read(fd_in, buf, buf_size);

		if (do_debug) {
			DDD0("Iteration: %d, finised read(), there is %d bytes\n", iteration, rx);
		}

		if (rx < 0) {
			if (do_debug) {
				DE("Could not read: from read sock %s [%d]: %s\n", name_in, fd_in, strerror(errno));
			}
			ret = -PEPA_ERR_BAD_SOCKET_READ;
			goto endit;
		}

		/* nothing to read*/
		if ((0 == rx) && (1 == iteration)) {
			/* If we can not read on the first iteration, it probably means the fd was closed */
			ret = -PEPA_ERR_BAD_SOCKET_READ;

			if (do_debug) {
				DE("Could not read on the first iteration: from read sock %s [%d]: %s\n", name_in, fd_in, strerror(errno));
			}

#if 0 /* SEB */
			/* Try to wait a bit */
			usleep(10000);

			rx = read(fd_in, buf, buf_size);
			if (0 == rx) {
				ret = -PEPA_ERR_BAD_SOCKET_READ;
			}
#endif

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
					DDDE("Could not write to write to sock %s [%d]: returned -1: %s\n", name_out, fd_out, strerror(errno));
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
			DDD0("Iteration %d done: rx = %d, tx = %d\n", iteration, rx, tx);
		}
		//} while (rx > 0); /* Tun this loop as long as we have data on read socket */

	} while ((int)buf_size == rx); /* Tun this loop as long as we have data on read socket */

	ret = PEPA_ERR_OK;
endit:
	if (do_debug) {
		DDD0("Finished transfering from %s to %s, returning %d, rx = %d, tx = %d, \n", name_in, name_out, ret, rx_total, tx_total);
	}
	return ret;
}


int pepa_one_direction_copy(pepa_fds_t *fdx, buf_t *buf)
{
	int ret       = PEPA_ERR_OK;
	int rx        = 0;
	int tx_total  = 0;
	int rx_total  = 0;
	int iteration = 0;

	do {
		int tx         = 0;
		int tx_current = 0;

		iteration++;

		DDD0("Going to read from %s fd %d to buf size %ld\n", fdx->name_read, fdx->fd_read, buf->room);

		/* Read one time, then we will transfer it possibly in pieces */
		rx = read(fdx->fd_read, buf->data, buf->room);
		fdx->reads++;

		if (rx < 0) {
			DE("Could not read: from %s %s\n", fdx->name_read, strerror(errno));
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

			//DD0("Read descriptor is empty\n");
			goto endit;
		}

		DDD0("Going to write to %s fd %d from buf size %d, used %d\n",
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
				DDDE("Could not write to %s: returned -1: %s\n", fdx->name_write, strerror(errno));
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
		DDD("%s -> %s written_acc %d, rd_acc = %d\n", fdx->name_read, fdx->name_write, written_acc, rd_acc);
	}
#endif
	return ret;
}

int pepa_test_fd(int fd)
{
	if ((fcntl(fd, F_GETFL) < 0) && (EBADF == errno)) {
		//DE("File descriptor %d is invalid: %s\n", fd, strerror(errno));
		return -1;
	}
	return 0;
}

int epoll_ctl_add(int epfd, int fd, uint32_t events)
{
	struct epoll_event ev;

	if (fd < 0) {
		DE("Tryed to add fd < 0: %d\n", fd);
		return -1;
	}

	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		// if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		DE("Can not add fd %d to epoll: %s\n", fd, strerror(errno));
		// perror("epoll_ctl()\n");
		//exit(1);
		return -1;
	}

	return 0;
}

#if 0 /* SEB */
static void pepa_one_direction_rw_thread_cleanup(void *arg){
	pepa_fds_t *fdx        = (pepa_fds_t *)arg;
	DDD("Running RW THREAD EXIT [%s] cleanup routine\n", fdx->my_name);

	/* We should close the read file descriptor if the flag 'close reading socket' is set */
	if (fdx->close_read_sock) {
		int close_rc = close(fdx->fd_read);
		if (0 != close_rc) {
			DE("%s: Could not close reading socket: %s\n", fdx->my_name, strerror(errno));
		}
	}

	if (fdx->buf) {
		int rc = buf_free(fdx->buf);
		if (BUFT_OK != rc) {
			DE("Could not release buf_t\n");
		}
	}

	if (fdx->fd_eventpoll) {
		close(fdx->fd_eventpoll);
	}

	DD("***** %-14s TRANSFER THREAD ENDED: TRANSFERED: reads: %-5lu, writes: %-5lurx bytes: %-10lu Kb:%-8lu tx: bytes: %-10lu Kb:%-8lu *****\n",
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
		DE("%s:Could not init CTL\n", fdx->my_name);
		pthread_exit(NULL);
	}

	DDD("%s: Starting: %s -> %s, fd read: %d, fd_write: %d\n",
		fdx->my_name, fdx->name_read, fdx->name_write, fdx->fd_read, fdx->fd_write);

	if (fdx->fd_read < 0 || fdx->fd_write < 0) {
		DE("%s: One of descriptors is invalid\n", fdx->my_name);
		pthread_exit(NULL);
	}

	/* EPLOLL */

	struct epoll_event events[2];
	int                epoll_fd  = epoll_create1(EPOLL_CLOEXEC);

	if (0 != epoll_ctl_add(epoll_fd, fdx->fd_read, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		DDE("%s: Tried to add fdx->fd_read = %d and failed\n", fdx->my_name, fdx->fd_read);
		pthread_exit(NULL);
	}

	fdx->fd_eventpoll = epoll_fd;

	if (epoll_ctl_add(epoll_fd, fdx->fd_write, EPOLLOUT | EPOLLRDHUP | EPOLLHUP)) {
		//if (epoll_ctl_add(epoll_fd, fdx->fd_write, EPOLLRDHUP | EPOLLHUP)) {
		DDE("%s: Tried to add fdx->fd_write = %d and failed\n", fdx->my_name,  fdx->fd_write);
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
			DE("%s: error on wait: %s\n", fdx->my_name, strerror(err));
			pthread_exit(NULL);
		}

		for (i = 0; i < event_count; i++) {

			/* If one of the read/write sockets is diconnected, exit the thread */
			if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
				DDDE("%s: Connection |%s = %d| is closed, terminate thread: %s\n\n", fdx->my_name,
					 (events[i].data.fd == fdx->fd_read) ? "read" : "write",
					 (events[i].data.fd == fdx->fd_read) ? fdx->fd_read : fdx->fd_write,
					 strerror(err));
				pthread_exit(NULL);
			}

			/* Read from socket */
			if ((events[i].data.fd == fdx->fd_read) && (events[i].events & EPOLLIN)) {
				rc = pepa_one_direction_copy(fdx, buf);
				if (rc < 0) {
					DDE("%s: Read/Write op between sockets failure: fd read: %d. fd out: %d\n",
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
			DDE("%s: read fd is invalid, terminate\n", fdx->my_name);
			pthread_exit(NULL);
		}

		if (0 != pepa_test_fd(fdx->fd_write)) {
			DDE("%s: write fd is invalid, terminate\n", fdx->my_name);
			pthread_exit(NULL);
		}
	#endif

		if ((fdx->reads > 0) && (0 == (fdx->reads % RX_TX_PRINT_DIVIDER))) {
			DD("===== %-14s TRANSFER THREAD WORKING: TRANSFERED: reads: %-7lu writes: %-7lurx bytes: %-10lu Kb:%-8lu tx: bytes: %-10lu Kb:%-8lu =====\n",
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
		return -1;
	}

	int rc = shutdown(sock, SHUT_RDWR);
	if (rc < 0) {
		DDDE("%s: Could not shutdown the socket: fd: %d, %s\n", my_name, sock, strerror(errno));
		return -1;
	}

	rc = close(sock);
	if (rc < 0) {
		DDDE("%s: Could not close the socket: fd: %d, %s\n", my_name, sock, strerror(errno));
		return -1;
	}

	close(sock);
	DDD("%s: Closed socket %d\n", my_name, sock);
	return 0;
}

__attribute__((nonnull(1, 2)))
int pepa_open_listening_socket(struct sockaddr_in *s_addr, const buf_t *ip_address, const int port, const int num_of_clients, const char *name)
{
	int rc   = PEPA_ERR_OK;
	int sock;

	DD("Open Socket [from %s]: starting for %s:%d, num of clients: %d\n",
	   name, ip_address->data, port, num_of_clients);

	memset(s_addr, 0, sizeof(struct sockaddr_in));
	s_addr->sin_family = (sa_family_t)AF_INET;
	s_addr->sin_port = htons(port);

	DD("Open Socket [from %s]: Port is %d\n", name, port);

	const int inet_aton_rc = inet_aton(ip_address->data, &s_addr->sin_addr);

	DD("Open Socket [from %s]: specified address, use %s\n", name, ip_address->data);
	/* Warning: inet_aton() returns 1 on success */
	if (1 != inet_aton_rc) {
		DE("Open Socket [from %s]: Could not convert string address to in_addr_t: %s\n", name, strerror(errno));
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	DDD0("Open Socket [from %s]: Going to create socket for %s:%d\n", name, ip_address->data, port);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock  < 0) {
		DE("Open Socket [from %s]: Could not create listen socket: %s\n", name, strerror(errno));
		//perror("could not create listen socket");
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	const int enable = 1;
	rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (rc < 0) {
		DE("Open Socket [from %s]: Could not set SO_REUSEADDR on socket, error: %s\n", name, strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}

#if 0 /* SEB */
	rc = setsockopt(sock, SOL_SOCKET, MSG_NOSIGNAL, (void *)&enable, sizeof(int));
	if (rc < 0) {
		DE("Open Socket [from %s]: Could not set SO_NOSIGPIPE on socket, error: %s\n", name, strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}
#endif

	do {
		rc = bind(sock, (struct sockaddr *)s_addr, (socklen_t)sizeof(struct sockaddr_in));
		if (rc < 0 && EADDRINUSE == errno) {
			DD("Address in use, can't bind, [from %s], waiting...\n", name);
			sleep(10);
		}
	} while (rc < 0 && EADDRINUSE == errno);

	if (rc < 0) {
		//perror("Can't bind: ");
		DE("Open Socket [from %s]: Can't bind: %s\n", name, strerror(errno));
		close(sock);
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_IN_USE);
	}

	rc = listen(sock, num_of_clients);
	if (rc < 0) {
		DE("Open Socket [from %s]: Could not set SERVER_CLIENTS: %s\n", name, strerror(errno));
		//perror("could not set SERVER_CLIENTS");
		close(sock);
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_LISTEN);
	}

	DDD("Open Socket [from %s]: Opened listening socket: %d\n", name, sock);
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
		DE("[from %s]: The string is not a valid IP address: |%s|\n", name, address);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	if (convert_rc < 0) {
		DE("[from %s]: Could not convert string addredd |%s| to binary\n", name, address);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	s_addr.sin_port = htons(port);

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		DE("[from %s]:: Could not create socket\n", name);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_CREATION);
	}

#if 0 /* SEB */
	const int enable = 1;
	int rc = setsockopt(sock, SOL_SOCKET, MSG_NOSIGNAL, (void *)&enable, sizeof(int));
	if (rc < 0) {
		DE("Open Socket [from %s]: Could not set SO_NOSIGPIPE on socket, error: %s\n", name, strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}
#endif


	if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
		DE("[from %s]: Could not connect to server: %s\n", name, strerror(errno));
		close(sock);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCK_CONNECT);
	}

	DD("[from %s]: Opened connection to server: %d\n", name, sock);

	return (sock);
}

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief This function analyses accept() failure
 * @param int error_code Error code saver in errno
 * @return int PEPA_ERR_OK if the accept() should retry,
 *  	   a negative value if it should be aborted
 * @details 
 */

#if 0 /* SEB */
static int pepa_analyse_accept_error(const int error_code){
	switch (error_code) {
		case EAGAIN:
		/* The socket is marked nonblocking and no connections are present to be accepted */
		DE("The socket is marked nonblocking and no connections are present to be accepted\n");
		return PEPA_ERR_OK;
		case EBADF: /* sockfd is not an open file descriptor */
		DE("sockfd is not an open file descriptor\n");
		return -1;
		break;
		case ECONNABORTED: /* A connection has been aborted. */
		DE("A connection has been aborted\n");
		return -1;
		break;
		case EFAULT:  /* The addr argument is not in a writable part of the user address space. */
		DE("The addr argument is not in a writable part of the user address space\n");
		return -1;
		break;
		case EINTR:  /* The system call was interrupted by a signal that was caught before a valid connection arrived; see signal(7). */
		DE("The system call was interrupted by a signal that was caught before a valid connection arrived\n");
		return PEPA_ERR_OK;
		break;
		case EINVAL: /* Socket is not listening for connections, or addrlen is invalid (e.g., is negative). */
		DE("Socket is not listening for connections, or addrlen is invalid (e.g., is negative)\n");
		return -1;
		break;
		case EMFILE: /* The per-process limit on the number of open file descriptors has been reached. */
		DE("The per-process limit on the number of open file descriptors has been reached\n");
		return PEPA_ERR_OK;
		break;
		case ENFILE: /* The system-wide limit on the total number of open files has been reached. */
		DE("The system-wide limit on the total number of open files has been reached\n");
		return PEPA_ERR_OK;
		break;
		case ENOBUFS:
		case ENOMEM:
		DE("Not enough free memory\n");
		return PEPA_ERR_OK;
		/* Not enough free memory.
		   This often means that the memory allocation is limited by the socket buffer limits,
		   not by the system memory. */
		break;
		case ENOTSOCK: /* The file descriptor sockfd does not refer to a socket. */
		DE("The file descriptor sockfd does not refer to a socket\n");
		return -1;
		break;
		case EOPNOTSUPP: /* The referenced socket is not of type SOCK_STREAM.*/
		DE("The referenced socket is not of type SOCK_STREAM\n");
		return -1;
		break;
		case EPROTO: /* Protocol error. */
		DE("Protocol error\n");
		return PEPA_ERR_OK;
		break;
		case EPERM: /*  Firewall rules forbid connection. */
		DE("Firewall rules forbid connection\n");
		return PEPA_ERR_OK;
		break;
		default:
		DE("Unknow error, should never see this message\n");
		/* Unknown error */
		return -1;
	}

	/* Again, never should be here */
	abort();
	return -1;
}
#endif

int pepa_start_threads(void)
{
	set_sig_handler();

	/* Slose STDIN */
	int fd = open("/dev/null", O_WRONLY);
	dup2(fd, 0);
	close(fd);

	/* Start CTL thread */
	// pepa_thread_start_ctl();

	/* Start SHVA thread */
	//pepa_shva_start();
	pepa_thread_start_out();
	pepa_thread_start_shva();
	pepa_thread_start_in();
	return 0;
}

int pepa_stop_threads(void)
{
	/* TODO */
	return -1;
}

