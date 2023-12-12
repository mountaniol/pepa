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


#include <errno.h>
#include "pepa_socket.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_debug.h"
#include "buf_t/buf_t.h"
#include "buf_t/se_debug.h"

#ifndef PPEPA_MAX
	#define PEPA_MAX(a,b) ( a > b ? a : b)
#endif

static void pepa_print_pthread_create_error(const int rc)
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

/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief Init x_connect_t structure and init it with values
 * @param int fd_left Left file descriptor
 * @param int fd_right Right file descriptor
 * @param size_t buf_size Buffer size to use; the buffer is used
 *  			 to read from one fd and write to another 
 * @return x_connect_t* Allocated and filled structure
 * @details 
 */

static x_connect_t *x_connect_t_alloc(int fd_src, int fd_dst, size_t buf_size)
{
	x_connect_t *xconn = calloc(sizeof(x_connect_t), 1);
	if (NULL == xconn) {
		DE("Can't allocate\n");
		PEPA_TRY_ABORT();
		return (NULL);
	}

	xconn->buf = buf_new(buf_size);
	if (NULL == xconn->buf) {
		DE("Can not allocate buf_t\n");
		PEPA_TRY_ABORT();
		free(xconn);
		return NULL;
	}

	xconn->fd_src = fd_src;
	xconn->fd_dst = fd_dst;

	return (xconn);
}
__attribute__((nonnull(1)))
static void x_connect_t_release(x_connect_t *xconn)
{
	const int rc = buf_free(xconn->buf);
	if (BUFT_OK != rc) {
		DE("Can not release buf_t object\n");
		PEPA_TRY_ABORT();
	}

	/* Secure way: clear memory before release it */
	memset(xconn, 0, sizeof(x_connect_t));
	free(xconn);
}

/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief Open a socket with all possible error checks
 * @param struct sockaddr_in* s_addr        
 * @param buf_t* ip_address    
 * @param int port          
 * @param int num_of_clients
 * @return int Opened socked file descriptor; a negative error
 *  	   code on error
 * @details 
 */
__attribute__((nonnull(1, 2)))
static int pepa_open_socket(struct sockaddr_in *s_addr, const buf_t *ip_address, const int port, const int num_of_clients)
{
	int rc   = PEPA_ERR_OK;
	int sock;

	DD("Open Socket: starting for %s:%d, num of clients: %d\n", ip_address->data, port, num_of_clients);

	memset(s_addr, 0, sizeof(struct sockaddr_in));
	s_addr->sin_family = (sa_family_t)AF_INET;
	s_addr->sin_port = htons(port);

	DD("Open Socket: Port is %d\n", port);

	const int inet_aton_rc = inet_aton(ip_address->data, &s_addr->sin_addr);

	DD("Open Socket: specified address, use %s\n", ip_address->data);
	/* Warning: inet_aton() returns 1 on success */
	if (1 != inet_aton_rc) {
		DE("Could not convert string address to in_addr_t\n");
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	DD("Open Socket: Going to create socket for %s:%d\n", ip_address->data, port);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock  < 0) {
		syslog(LOG_ERR, "could not create listen socket: %s\n", strerror(errno));
		perror("could not create listen socket");
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	rc = bind(sock, (struct sockaddr *)s_addr, (socklen_t)sizeof(struct sockaddr_in));
	if (rc < 0) {
		syslog(LOG_ERR, "Can't bind: %s\n", strerror(errno));
		perror("Can't bind");
		close(sock);
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_BIND);
	}

	rc = listen(sock, num_of_clients);
	if (rc < 0) {
		syslog(LOG_ERR, "could not set SERVER_CLIENTS: %s\n", strerror(errno));
		perror("could not set SERVER_CLIENTS");
		close(sock);
		// PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_LISTEN);
	}
	return (sock);
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Open connection to shva, return file descriptor
 * @return int file descriptor of socket to shva, >= 0;
 *  	   a negative error code on an error
 */
static int pepa_open_shava_connection(void)
{
	struct sockaddr_in s_addr;
	int                sock;
	pepa_core_t        *core  = pepa_get_core();

	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = (sa_family_t)AF_INET;

	DD("1\n");

	// const int convert_rc = inet_pton(AF_INET, ip->ip, &s_addr.sin_addr);
	const int convert_rc = inet_pton(AF_INET, core->shva_thread.ip_string->data, &s_addr.sin_addr);
	if (0 == convert_rc) {
		DE("The string is not a valid IP address: |%s|\n", core->shva_thread.ip_string->data);
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	DD("2\n");

	if (convert_rc < 0) {
		DE("Could not convert string addredd |%s| to binary\n", core->shva_thread.ip_string->data);
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}
	DD("3\n");

	// s_addr.sin_port = htons(ip->port);
	s_addr.sin_port = htons(core->shva_thread.port_int);

	DD("4\n");

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		DE("could not create socket\n");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	DD("5\n");

	if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
		DE("could not connect to server\n");
		close(sock);
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCK_CONNECT);
	}

	DD("6\n");
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

__attribute__((pure))
static int pepa_analyse_accept_error(const int error_code)
{
	switch (error_code) {
	case EAGAIN:
		/* The socket is marked nonblocking and no connections are present to be accepted */
		DE("The socket is marked nonblocking and no connections are present to be accepted\n");
		return -1;
		break;
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
		return -1;
		break;
	case ENFILE: /* The system-wide limit on the total number of open files has been reached. */
		DE("The system-wide limit on the total number of open files has been reached\n");
		return -1;
		break;
	case ENOBUFS:
	case ENOMEM:
		DE("Not enough free memory\n");
		return -1;
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
		return -1;
		break;
	case EPERM: /*  Firewall rules forbid connection. */
		DE("Firewall rules forbid connection\n");
		return -1;
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

/* TCP max socket size is 1 GB */
#define MAX_SOCKET_SIZE (0x40000000)
__attribute__((hot, nonnull(1)))
static int pepa_copy_between_sockects_execute(const x_connect_t *xcon)
{
	ssize_t rc;
	int     iteration = 0;

	do {
		do { /* Care receiving is interrupted by signal */
			rc = recv(xcon->fd_src, xcon->buf->data, xcon->buf->room, 0);
		} while (rc < 0 && EINTR == errno);

		/* If we can not read on the first iteration, it means the fd is closed */
		if ((0 == iteration) && (0 == rc)) {
			DE("Probably closed socket 'from'\n");
			return (-PEPA_ERR_SOCKET_READ_CLOSED);
		}

		/* Detect the error */
		if (rc < 0) {
			DE("Error on reading from socket: %s\n", strerror(errno));
			switch (errno) {
			case EBADF: /* The argument sockfd is an invalid file descriptor */
				return (-PEPA_ERR_BAD_SOCKET_READ);
			case ECONNREFUSED: /* A remote host refused to allow the network connection (typically because it is not running the requested service) */
				return (-PEPA_ERR_SOCKET_READ);
			case EINVAL: /* Invalid argument passed */
				return (-PEPA_ERR_SOCKET_READ);
			case ENOTCONN: /* The socket is associated with a connection-oriented protocol and has not been connected */
				return (-PEPA_ERR_BAD_SOCKET_READ);
			case ENOTSOCK: /* The file descriptor sockfd does not refer to a socket */
				return (-PEPA_ERR_BAD_SOCKET_READ);
			default:
				return (-PEPA_ERR_SOCKET_READ);
			}
		}

		xcon->buf->used = rc;

		do { /* Care sending is interrupted by signal */
			rc = send(xcon->fd_dst, xcon->buf->data, xcon->buf->used, 0);
		} while (rc < 0 && (EINTR == errno));

		if (rc < 0) {
			DE("Error on writing to socket: %s\n", strerror(errno));
			switch (errno) {
			case ECONNRESET: /* Connection is closed by peer */
				return (-PEPA_ERR_SOCKET_WRITE_CLOSED);
			case EDESTADDRREQ:  /* The socket is not connection-mode, and no peer address is set */
				return (-PEPA_ERR_BAD_SOCKET_WRITE);
			case EFAULT: /* An invalid user space address was specified for an argument */
				return (-PEPA_ERR_SOCKET_WRITE);
			case EPIPE: /* It is the same as ENOTCONN in Linux */
			case ENOTCONN: /* The socket is not connected, and no target has been given */
				return (-PEPA_ERR_BAD_SOCKET_WRITE);
			case  ENOTSOCK: /* The file descriptor sockfd does not refer to a socket*/
				return (-PEPA_ERR_BAD_SOCKET_WRITE);
			default: /* Any other error? */
				perror("Error on sending: ");
				return (-PEPA_ERR_SOCKET_WRITE);
			}
		}

		if (0 == rc) {
			DE("Probably closed file descriptor of 'write' socket:\n");
			return (-PEPA_ERR_SOCKET_WRITE_CLOSED);
		}

		iteration++;
	} while (rc > 0);

	return (PEPA_ERR_OK);
}

/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief This function takes two file descriptors (of sockets),
 *  	  and passes buffers between them as a tunnel.
 *  	  It is infinite loop function.
 * @param int fd_left Socket file descriptor
 * @param int fd_right Socket file descriptor
 * @return int Status of an error. This function suppposed to
 *  	   run in infinite loop. If it returned, it signalizes
 *  	   the reason of the return. The statuscan be also
 *  	   -PEPA_ERR_EVENT which means a event from Control
 *  		thread received.
 * @details 
 */
__attribute__((hot))
static int pepa_copy_between_sockects(const int fd_src, const int fd_event, const int fd_dst, const int buf_size)
{
	/* We need two buffer:  */
	int         rc;

	/* Allocate x_connect_t struct */
	/* TODO: The size of the buffer should be configurable */
	x_connect_t *xconn = x_connect_t_alloc(fd_src, fd_dst, buf_size);
	TESTP(xconn, -PEPA_ERR_ALLOCATION);

	while (1) {
		int            max_fd;
		fd_set         read_set;
		struct timeval tv;

		tv.tv_sec = 5;
		tv.tv_usec = 0;

		/* Socket sets */
		FD_ZERO(&read_set);

		/* Read set, signaling when there is data to read */
		FD_SET(fd_src, &read_set);
		FD_SET(fd_event, &read_set);

		max_fd = PEPA_MAX(fd_src, fd_event);
		DDD("fd_src = %d, fd_event = %d, fd_max = %d\n", fd_src, fd_event, max_fd);

		rc = select(max_fd + 1, &read_set, NULL, NULL, &tv);

		if (FD_ISSET(fd_event, &read_set)) {
			DD("Got event from CTL\n");
			return (-PEPA_ERR_EVENT);
		}

		/* Data is ready on the src file descriptor:
		   read it from the left fd, write to the right: the RIGHT direction */
		if (FD_ISSET(fd_src, &read_set)) {
			rc = pepa_copy_between_sockects_execute(xconn);
			if (PEPA_ERR_OK != rc) {
				x_connect_t_release(xconn);
				return (rc);
			}
		}
	}

	return (rc);
}

static pepa_in_thread_fds_t *pepa_in_thread_fds_t_alloc(void)
{
	pepa_core_t          *core = pepa_get_core();
	pepa_in_thread_fds_t *fds  = malloc(sizeof(pepa_in_thread_fds_t));
	if (NULL == fds) {
		DE("Can't allocate\n");
		return (NULL);
	}

	memset(fds, 0, sizeof(pepa_in_thread_fds_t));
	fds->buf_fds = buf_array(sizeof(int), core->internal_buf_size);
	if (NULL == fds->buf_fds) {
		DE("Can not allocate buf_t\n");
		free(fds);
		PEPA_TRY_ABORT();
		return NULL;
	}

	int sem_rc = sem_init(&fds->buf_fds_mutex, 0, 1);

	if (0 != sem_rc) {
		DE("Could not init mutex\n");
		perror("sem init failure: ");
		int rc = buf_free(fds->buf_fds);
		if (BUFT_OK != rc) {
			DE("Could not release buf_t; possible memory leak\n");
		}
		free(fds);
		PEPA_TRY_ABORT();
		return (NULL);
	}

	/* We need the event fd to signalize between IN and Acceptor threads about new connection accepted */
	fds->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	/* -1 returned in case the eventfd can not be created */
	if (-1 == fds->event_fd) {
		DE("Could not open eventfd\n");
		int rc = buf_free(fds->buf_fds);
		if (BUFT_OK != rc) {
			DE("Could not release buf_t; possible memory leak\n");
		}
		free(fds);
		DE("Could not open eventfd\n");
		PEPA_TRY_ABORT();
		return (NULL);
	}

	fds->socket = -1;
	return (fds);
}

__attribute__((unused, nonnull(1)))
static void pepa_in_thread_fds_t_release(pepa_in_thread_fds_t *fds)
{
	if (NULL == fds) {
		DE("Arg is NULL\n");
		PEPA_TRY_ABORT();
		return;
	}

	close(fds->event_fd);

	int rc = sem_destroy(&fds->buf_fds_mutex);
	if (0 != rc) {
		DE("Could not destroy mutex\n");
		perror("sem destroy failure: ");
		PEPA_TRY_ABORT();
		return;
	}

	if (fds->buf_fds) {
		rc = buf_free(fds->buf_fds);
		if (BUFT_OK != rc) {
			DE("Could not destroy buf_t, memory leas is possible\n");
			PEPA_TRY_ABORT();
		}
	}

	/* Secure way: clear memory before release it */
	memset(fds, 0, sizeof(pepa_in_thread_fds_t));
	free(fds);
}

/* Send event */
static void pepa_event_send(int fd, uint64_t code)
{
	int      rc;

	/* Set event */
	do {
		rc = write(fd, &code, sizeof(code));
		if (EAGAIN == rc) {
			DD("Write to eventfd returned EAGAIN\n");
		} else {
			DD("Wrote OK an event to eventfd\n");
		}
	} while (EAGAIN == rc);
}

static void pepa_event_rm(int fd)
{
	uint64_t val           = 1;
	__attribute__((unused))int      rc;

	/* Set event */
	rc = read(fd, &val, sizeof(val));
}

__attribute__((unused))
static int pepa_event_check(int fd)
{
	uint64_t val           = 1;
	__attribute__((unused))int      rc;

	/* Set event */
	rc = read(fd, &val, sizeof(val));
	if (rc == sizeof(val)) {
		return 1;
	}

	return 0;
}

__attribute__((nonnull(1)))
static int pepa_acceptor_event_on(pepa_in_thread_fds_t *fds)
{
	pepa_event_send(fds->event_fd, 1);
	return PEPA_ERR_OK;
}

__attribute__((nonnull(1)))
static void pepa_acceptor_event_off(const pepa_in_thread_fds_t *fds)
{
	pepa_event_rm(fds->event_fd);
}

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief This function wait for event from acceptor. When event
 *  	  received, it returns PEPA_ERR_OK_OK status
 * @param pepa_in_thread_fds_t* fds   Pointer to structure
 *  						  containing the event fd
 * @param size_t sec   How many seconds to wait
 * @param size_t usec  How many ms to wait
 * @return int PEPA_ERR_OK if event happened; 1 if event not
 *  	   happened and it exits by timeout; A negative value on
 *  	   error
 */
__attribute__((nonnull(1)))
int pepa_wait_for_signal_from_acceptor(const pepa_in_thread_fds_t *fds, const size_t sec, const size_t usec)
{
	/* Select related variables */
	fd_set         rfds;
	struct timeval tv;
	int            retval;

	do {
		FD_ZERO(&rfds);
		FD_SET(fds->event_fd, &rfds);

		/* Init the timer to wait user specified time */
		tv.tv_sec = sec;
		tv.tv_usec = usec;

		/* Wait a signal in the read set */
		retval = select(fds->event_fd + 1, &rfds, NULL, NULL, &tv);

		/* Timeout */
		if (retval == 0) {
			return 1;
		}

		/* A signal interrupted us */
		if ((retval == -1) && (errno == EINTR)) {
			continue;
		}

		/* Another error, not interrupt - we can not continue */
		if (retval == -1) {
			return -1;
		}

		/* This is absolutely unaceptable situation, and means we have some
		 * serious bug - this should not happen: we know that we have one
		   file descriptor, and the event must be there and only there  */
		if (!FD_ISSET(fds->event_fd, &rfds)) {
			DE("Something bad happened\n");
		}

		/* We ignore signals */
	} while (retval < 1);

	if (retval == -1) {
		return retval;
	}

	/* We know that there is a buffer,
	   we get signal when the FIFO is empty and the first buffer inserted */
	if (FD_ISSET(fds->event_fd, &rfds)) {
		return PEPA_ERR_OK;
	}

	return 1;
}

__attribute__((nonnull(1, 2)))
int pepa_merge_buffers(buf_t *buf_dst, pepa_in_thread_fds_t *fds)
{
	int rc;

	/* Lock sempaphore: we don't want Acceptor touch the buffer during the merge  */
	sem_wait(&fds->buf_fds_mutex);
	rc = buf_arr_merge(buf_dst, fds->buf_fds);
	sem_post(&fds->buf_fds_mutex);

	if (PEPA_ERR_OK != rc) {
		DE("Could not merge buffers: %s\n", buf_error_code_to_string(rc));
		PEPA_TRY_ABORT();
		return -1;
	}
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/9/23)
 * @brief This thread waiting on IN socket accept(),
 *  	  create a new sockets, and adds them to the shared
 *  	  array.
 * @param void* arg   Structure pepa_in_thread_fds_t which
 *  		  created in IN thread ans shared between IN and
  *  		  Acceptor; this structure contains array of
 *  		  accepted  file descriptors
 * @return void* 
 * @details We need this Acceptor thread because accept() call
 *  		is blocking. So we need to accept new socket and
 *  		also transfer data from IN sockets to SHVA server.
 *  		This thread accepts sockets, when IN thread it data
 *  		transfering thread.
 */
void *pepa_in_thread_acceptor(__attribute__((unused))void *arg)
{
	int                  rc;
	pepa_core_t          *core = pepa_get_core();
	pepa_in_thread_fds_t *fds  = core->acceptor_shared;

	TESTP(fds, NULL);

	DDD("Thread Acceptor: Detaching\n");
	if (0 != pthread_detach(pthread_self())) {
		DE("Thread Acceptor: can't detach myself\n");
		perror("Thread Acceptor: can't detach myself");
		PEPA_TRY_ABORT();
	}

	DDD("Thread Acceptor: Detached\n");

	rc = pthread_setname_np(pthread_self(), "ACCEPTOR THREAD");
	if (0 != rc) {
		DE("Thread SHVA: can't set name\n");
	}

	DDD("Thread Acceptor: Starting the thread\n");
	while (1) {
		struct sockaddr addr;
		socklen_t       addrlen = sizeof(struct sockaddr);
		DDD("Thread Acceptor: Entering accept()\n");
		int             fd      = accept(fds->socket, (struct sockaddr *)&addr, &addrlen);
		DDD("Thread Acceptor: Exit from accept(), got fd: %d\n", fd);

		/* If something went wrong, analyze the error and decide what to do */
		if (fd < 0) {
			DE("Thread Acceptor: Could not accept incoming connection; ignoting it\n");
			continue;
		}

		DDD("Thread Acceptor: Going to add the new fd %d to the shared buffer\n", fd);
		sem_wait(&fds->buf_fds_mutex);
		int rc = buf_arr_add(fds->buf_fds, &fd);
		sem_post(&fds->buf_fds_mutex);

		if (BUFT_OK != rc) {
			DE("Thread Acceptor: Could not set new member (fd: %d) in buffer array\n", fd);
			PEPA_TRY_ABORT();
		}

		DDD("Thread Acceptor: Added the new fd %d to the shared buffer\n", fd);

		/* Send signal to the IN thread that new file dscriptors are ready */
		DDD("Thread Acceptor: Goinf to turn on the event fd\n");
		pepa_acceptor_event_on(fds);
		DDD("Thread Acceptor: Turned on the event fd\n");

	} /* while (1) */

	DE("Thread Acceptor: Finishing the thread; should never be here\n");
	pthread_exit(NULL);
}

__attribute__((nonnull(1, 2)))
static int pepa_in_thread_create_socket(pepa_core_t *core, struct sockaddr_in *s_addr)
{
	int i;
	for (i = 0; i < 100; i++) {
		core->acceptor_shared->socket = pepa_open_socket(s_addr, core->in_thread.ip_string, core->in_thread.port_int, core->in_thread.clients);
		if (core->acceptor_shared->socket < 0) {
			DE("Thread IN: Can not create SHVA socket\n");
			sleep(10);
		} else {
			/* We got the file descriptor */
			return PEPA_ERR_OK;
		}
	}

	/* After 100 * 10 seconds of waiting we can not create the socket descriptor; terminate? */
	if (core->acceptor_shared->socket < 0) {
		DE("Thread IN: Can not create SHVA socket, should exit\n");
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	/* It should never be here; however, we set it here to prevent compiler warning */
	return (PEPA_ERR_OK);
}

__attribute__((hot, nonnull(1, 2)))
static int pepa_in_thread_send_data(pepa_core_t *core, x_connect_t *xconn, int fd_members, fd_set read_set)
{
	int index;
	/* Run on every file descriptor, test them; if there are buffer, send them to SHVA */
	for (index = 0; index < fd_members; index++) {
		int *fd = buf_arr_get_member_ptr(core->buf_in_fds, index);

		/* If current file descriptor was not changed, continue to the next iteration of the for()*/
		if (!FD_ISSET(*fd, &read_set)) {
			continue;
		} /* if (FD_ISSET(*fd, &read_set) */

		/* Set the file descriptor to use, we should transfer buffer from there to SHVA */
		xconn->fd_src = *fd;

		/* Send data from this file descriptor */
		int rc = pepa_copy_between_sockects_execute(xconn);

		/* If everithing allright, continue to the next file descriptor */
		if (PEPA_ERR_OK == rc) {
			continue;
		}

		/*** Error hadling ***/

		/* Can not read from the file descriptor; probably it was closed */
		if (PEPA_ERR_SOCKET_READ_CLOSED == rc) {
			/* Remove the socket */
			rc = buf_arr_rm(core->buf_in_fds, index);
			/* And close the socket file descriptor */
			close(*fd);

			if (BUFT_OK != rc) {
				DE("Could not remove member from the buf array\n");
				PEPA_TRY_ABORT();
				return -1;
			}
		}
	} /* for (index = 0; fd_members; index++) */
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/9/23)
 * @brief This is IN thread implementation.
 * @param void* arg Ignored
 * @return void*  Ignored
 * @details Thiw thread opens listening socket for IN stream,
 *  		and wait for incoming connection.
 * It accepts all connection, add the sockets fd to array,
 * and execute transfer from IN to SHVA
 */
void *pepa_in_thread(__attribute__((unused))void *arg)
{
	int                rc;
	pepa_core_t        *core  = pepa_get_core();
	struct sockaddr_in s_addr;
	core->acceptor_shared = pepa_in_thread_fds_t_alloc();
	pthread_t   acceptor_thread;
	x_connect_t *xconn          = NULL;

	/*** Init thread ****/

	DDD("Thread IN: Detaching\n");
	if (0 != pthread_detach(pthread_self())) {
		DE("Thread IN: can't detach myself\n");
		perror("Thread IN: can't detach myself");
		PEPA_TRY_ABORT();
	}

	DDD("Thread IN: Detached\n");

	rc = pthread_setname_np(pthread_self(), "IN THREAD");
	if (0 != rc) {
		DE("Thread SHVA: can't set name\n");
	}

	/* TODO: Make it configurable */

	/*** Open listening socket ****/

	DDD("Thread IN: Going to create IN socket\n");

	rc = pepa_in_thread_create_socket(core, &s_addr);

	/* After 100 * 10 seconds of waiting we can not create the socket descriptor; terminate? */
	if (PEPA_ERR_OK != rc) {
		DE("Thread IN: Can not create SHVA socket, should exit\n");
	}

	DDD("Thread IN: Created the IN socket\n");

	/*** Create Acceptor socket ***/

	DDD("Thread IN: Going to create the Acceptor thread\n");
	/* Create an additional thread to accept new connections */

	rc = pthread_create(&acceptor_thread, NULL, pepa_in_thread_acceptor, NULL);
	if (0 != rc) {
		DE("Thread IN: Could not create the acceptor thread, terminating\n");
		pepa_print_pthread_create_error(rc);
		PEPA_TRY_ABORT();
		pthread_exit(NULL);
	}

	DDD("Thread IN: The Acceptor thread is created\n");

	/*** Now run the loop until we have the first IN socket connected  ***/

	DDD("Thread IN: Going to wait for the first IN connection\n");
	do {
		rc = pepa_wait_for_signal_from_acceptor(core->acceptor_shared, 10, 0);
	} while (PEPA_ERR_OK != rc);

	/* Test what happened? */

	if (rc < 0) {
		DE("Thread IN: Error happened when waited signal from Acceptor\n");
		PEPA_TRY_ABORT();
		pthread_exit(NULL);
	}

	DDD("Thread IN: The first IN connection detected\n");

	/* Allocate local array buffer */
	if (NULL == core->buf_in_fds) {
		core->buf_in_fds = buf_array(sizeof(int), 0);
	}

	if (NULL == core->buf_in_fds) {
		DE("Thread IN: Can not allocate buf_t\n");
		PEPA_TRY_ABORT();
		pthread_exit(NULL);
	}

	/* Prepare the xconn structure: we always copy to shava socket */
	xconn = x_connect_t_alloc(core->shva_thread.fd_listen, 0, core->internal_buf_size);
	if (NULL == xconn) {
		DE("Thread IN: Can not create x_connect_t structure\n");
		PEPA_TRY_ABORT();
		pthread_exit(NULL);
	}

	DDD("Thread IN: Starting infinite select loop()\n");
	/* Here: we have at least one IN socket connected */
	while (1) {
		int            index;
		int            fd_members;
		int            fd_max     = 0;
		fd_set         read_set;
		struct timeval tv;

		tv.tv_sec = 0;
		tv.tv_usec = 5;

		/*
		 * Merge buffers before we started:
		 * We always should fo it.
		 * The internal loop will be interrupted in case the Acceptor got new file descriptors.
		 */
		rc = pepa_merge_buffers(core->buf_in_fds, core->acceptor_shared);

		if (PEPA_ERR_OK != rc) {
			DE("Could not merge buffers; continue however\n");
			PEPA_TRY_ABORT();
		}

		/* Clear select set */
		FD_ZERO(&read_set);

		/* Read all members in the file descriptors array, and set them into select set.
		 * A bit later we also add even fd into the set, so it never can be empty,
		 * even if no listening sockets are opened yet
		 */
		fd_members = buf_arr_get_members_count(core->buf_in_fds);
		for (index = 0; fd_members; index++) {
			int *fd = buf_arr_get_member_ptr(core->buf_in_fds, index);
			FD_SET(*fd, &read_set);
			if (fd_max < *fd) {
				fd_max = *fd;
			}
		}

		/* Add event fd into the set */
		FD_SET(core->acceptor_shared->event_fd, &read_set);

		/* ALso listen signal from Control thread */
		FD_SET(core->controls.in_from_ctl, &read_set);

		fd_max = PEPA_MAX(core->acceptor_shared->event_fd, fd_max);
		fd_max = PEPA_MAX(core->controls.in_from_ctl, fd_max);

		/*** The second level loop, wait on select and process ***/

		do {
			/* In the worst case we have only event fd in this set */
			int rc = select(fd_max + 1, &read_set, NULL, NULL, &tv);

			/* If returned 0 means no change in file descriptors */
			if (0 == rc) {
				continue;
			}

			/* If we exited with error and it is EINTR it means we interrupted by a signal;
			 * Ignore it and continue */
			if ((-1 == rc) && (errno == EINTR)) {
				continue;
			}

			/* Allrighty, if this is not a signal but another error, this is a bad thing */
			if (rc < 0) {
				DE("Got an error from select; we can't fix it\n");
				abort();
			}

			/*** Test events from Acceptor ***/

			if (FD_ISSET(core->controls.in_from_ctl, &read_set)) {
				/* Turn off one event on the event fd, we got the signal, thank you */
				pepa_event_rm(core->controls.in_from_ctl);
				DD("Got STOP event from Control thread\n");
				goto cancel_it;
				break;
			}

			/* If we got signal from Acceptor, we need to get new file descriptors;
			 * we just break the loop, and upped while() loop care about it.
			 * Dont't worry, dear reader, if there are real buffers on the real file descriptors,
			   we will read them in short time, on the next iteration  */
			if (FD_ISSET(core->acceptor_shared->event_fd, &read_set)) {
				/* Turn off one event on the event fd, we got the signal, thank you */
				pepa_acceptor_event_off(core->acceptor_shared);
				break;
			}

			/* Now let's test, how many members in the local file descriptors array? */
			fd_members = buf_arr_get_members_count(core->buf_in_fds);


			/* If buffer of file descriptors is empty, we break the loop */
			if (0 == fd_members) {
				break;
			}

			/*** Process file descriptors, send data to SHVA  ***/
			rc = pepa_in_thread_send_data(core, xconn, fd_members, read_set);
			if (PEPA_ERR_OK != rc) {
				DE("Error in processing file descriptors\n");
				break;
			}
		} while (buf_arr_get_members_count(core->buf_in_fds) > 0);
	}

cancel_it:
	/* Here: cloae everything and exit the thread */
	DDD("Thread IN: Finishing the thread\n");

	/* Terminate Acceptor thread */
	rc = pthread_cancel(core->acceptor_thread.thread_id);
	if (rc < 0) {
		DE("Could not teminate acceptor thread\n");
	}

	/* Release xconn */
	x_connect_t_release(xconn);

	/* Close all file descriptors and release buffers */
	if (NULL != core->buf_in_fds) {
		int index;
		for (index = 0; index < buf_arr_get_members_count(core->buf_in_fds); index++) {
			const int *fd = buf_arr_get_member_ptr(core->buf_in_fds, index);
			rc = close(*fd);
			if (0 != rc) {
				DE("Can not close IN read socket fd (%d), error: %d\n", *fd, rc);
			}
		}

		rc = buf_free(core->buf_in_fds);
		if (0 != rc) {
			DE("Can not free buf_array, memory leak possible: %s\n",
			   buf_error_code_to_string(rc));
		}
		core->buf_in_fds = NULL;
	}

	pthread_exit(NULL);
}

/* This function returns the state machine to the disconnected state */
/**
 * @author Sebastian Mountaniol (12/11/23)
 * @brief Finish all thread but SHVA; Close all sockets.
 * @details 
 */
static void pepa_back_to_disconnected_state(void)
{
	int                  rc;
	int                  index;
	pepa_core_t          *core = pepa_get_core();
	pepa_in_thread_fds_t *fds  = core->acceptor_shared;

	DD("BACK TO DISCONNECTED STATE\n");
	pepa_core_lock();
	sem_wait(&fds->buf_fds_mutex);

	/* Close IN thread and Acceptor thread */
	rc = pthread_cancel(core->in_thread.thread_id);
	if (0 != rc) {
		DE("Can not cancel IN thread\n");
	} else {
		DDD("Terminated IN thread\n");
	}

	core->in_thread.thread_id = -1;
	rc = pthread_cancel(core->acceptor_thread.thread_id);
	if (0 != rc) {
		DE("Can not cancel Acceptor thread\n");
	} else {
		DDD("Terminated Acceptor thread\n");
	}
	core->acceptor_thread.thread_id = -1;

	/* Close all Acceptor file descriptors */
	if (NULL != fds->buf_fds) {
		for (index = 0; index < buf_arr_get_members_count(fds->buf_fds); index++) {
			const int *fd = buf_arr_get_member_ptr(fds->buf_fds, index);
			rc = close(*fd);
			if (0 != rc) {
				DE("Can not close Acceptor read socket fd (%d), error: %d\n", *fd, rc);
			}
		}

		rc = buf_free(fds->buf_fds);
		if (0 != rc) {
			DE("Can not free buf_array, memory leak is possible: %s\n",
			   buf_error_code_to_string(rc));
		}
		fds->buf_fds = NULL;
	}

	/* We do not need the semaphore anymore */
	sem_post(&fds->buf_fds_mutex);

	/* Close all IN file descriptors */
	if (NULL != core->buf_in_fds) {
		for (index = 0; index < buf_arr_get_members_count(core->buf_in_fds); index++) {
			const int *fd = buf_arr_get_member_ptr(core->buf_in_fds, index);
			rc = close(*fd);
			if (0 != rc) {
				DE("Can not close IN read socket fd (%d), error: %d\n", *fd, rc);
			}
		}

		rc = buf_free(core->buf_in_fds);
		if (0 != rc) {
			DE("Can not free buf_array, memory leak possible: %s\n",
			   buf_error_code_to_string(rc));
		}
		core->buf_in_fds = NULL;
	}

	/* Close the IN socket */
	if (core->in_thread.fd_listen > 0) {
		rc = close(core->in_thread.fd_listen);
		if (0 != rc) {
			DE("Can not close listening socket (%d) of IN thread, error %d\n",
			   core->in_thread.fd_listen, rc);
		}
		core->in_thread.fd_listen = -1;
	}

	/* Close the SHVA socket */
	if (core->shva_thread.fd_listen > 0) {
		rc = close(core->shva_thread.fd_listen);
		if (0 != rc) {
			DE("Can not close listening socket (%d) of IN thread, error %d\n",
			   core->shva_thread.fd_listen, rc);
		}
		core->shva_thread.fd_listen = -1;
	}
	pepa_core_unlock();
}

static void pepa_shva_prepare_core(void)
{
	pepa_core_t *core = pepa_get_core();
	core->out_thread.fd_listen = -1;
	core->out_thread.fd_read = -1;
	core->out_thread.fd_write = -1;
	core->shva_thread.fd_listen = -1;
	core->shva_thread.fd_read = -1;
	core->shva_thread.fd_write = -1;
}

__attribute__((nonnull(1)))
static int pepa_shva_wait_first_OUT_connection(struct sockaddr *s_addr)
{
	pepa_core_t *core = pepa_get_core();
	while (core->out_thread.fd_read < 0) {
		int       error_action = PEPA_ERR_OK;

		socklen_t addrlen      = sizeof(struct sockaddr);

		DDD("Thread SHVA: Starting OUT 'accept' waiting\n");
		core->out_thread.fd_read = accept(core->out_thread.fd_listen, s_addr, &addrlen);
		DDD("Thread SHVA: Accepted OUT connection\n");

		/* If something went wrong, analyze the error and decide what to do */
		if (core->out_thread.fd_read < 0) {
			DE("Thread SHVA: Some error happend regarding accepting WAITING connection\n");
			error_action = pepa_analyse_accept_error(errno);
		}

		if (error_action < 0) {
			DE("Thread SHVA: An error occured, can not continue: %s\n", pepa_error_code_to_str(error_action));
			pepa_back_to_disconnected_state();
			return -1;
		}
		DDD("Thread SHVA: Contunue OUT connection waiting loop\n");
	}
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief This function is passed to pthreade_create as
 *  	  agrument; it runs communication between SHVA server
 *  	  and OUT stream
 * @param void* arg   Ignored
 * @return int Ignored
 * @details This function will pass all data from SHVA to OUT.
 *  		It run in infinite loop. Tte thread is teminated
 *  		when connection to SHVA or connection to OUT is
 *  		terminated.
 */
void *pepa_shva_thread(__attribute__((unused))void *arg)
{
	int                rc;
	pepa_core_t        *core  = pepa_get_core();
	struct sockaddr_in s_addr;

	/*** Init the thread ****/

	DDD("Thread SHVA: Detaching\n");
	if (0 != pthread_detach(pthread_self())) {
		DE("Thread SHVA: can't detach myself\n");
		perror("Thread SHVA: can't detach myself");
		PEPA_TRY_ABORT();
		abort();
	}

	DDD("Thread SHVA: Detached\n");

	rc = pthread_setname_np(pthread_self(), "SHVA THREAD");
	if (0 != rc) {
		DE("Thread SHVA: can't set name\n");
	}

	/* This is the main loop of this thread */

	/*** Init core variables before the main loop is started */

	pepa_shva_prepare_core();

	DDD("Thread SHVA: Waiting for OUT incoming connection\n");

	/*** Starting main loop ***/

	while (1) {

		/*** Create OUT listening socket ***/

		DDD("Thread SHVA: Going to create out socket\n");
		if (core->out_thread.fd_listen < 0) {
			core->out_thread.fd_listen = pepa_open_socket(&s_addr, core->out_thread.ip_string, core->out_thread.port_int, 1);
			if (core->out_thread.fd_listen < 0) {
				DE("Can not create SHVA socket\n");
				continue;
			}
		}

		DDD("Thread SHVA: Out socket created\n");

		/*** Wait until the OUT the first connection ***/

		/* Acept OUT connection */
		rc = pepa_shva_wait_first_OUT_connection((struct sockaddr *)&s_addr);
		if (PEPA_ERR_OK != rc) {
			pepa_back_to_disconnected_state();
			continue;
		}

		/*** Create SHVA socket ****/

		/* 4. Connect to SHVA server */
		DDD("Thread SHVA: Finished with OUT: there is connection\n");
		DDD("Thread SHVA: Going to create SHVA socket\n");
		core->shva_thread.fd_write = pepa_open_shava_connection();

		if (core->shva_thread.fd_write < 0) {
			DE("Can not open connection to SHVA server\n");
			/* Here we shoudl close connection to OUT socket and wait for the new OUT connection */
			close(core->out_thread.fd_write);
			core->out_thread.fd_write = -1;
			continue;
		}


		/*** Create the IN listening thread ****/

		DDD("Thread SHVA: Created SHVA socket\n");

		/* Here we can start transfer SHVA -> OUT; before we do it, we start IN thread */
		DDD("Thread SHVA: Going to start the IN thread\n");
		const int in_create_rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_in_thread, NULL);
		if (0 != in_create_rc) {
			DE("Something wrong witb IN thread, not created; terminate here\n");
			pepa_print_pthread_create_error(in_create_rc);
			perror("Can not create IN thread: ");
			pepa_back_to_disconnected_state();
			continue;
		}

		DDD("Thread SHVA: Created the IN thread\n");

		/*** Enter infinite loop ****/

		DDD("Thread SHVA: Starting the internal loop copy_between_sockects\n");
		int rc = pepa_copy_between_sockects(core->shva_thread.fd_write,
											core->controls.shva_from_ctl,
											core->out_thread.fd_listen,
											core->internal_buf_size);

		/*** The inifnite loop was interrupted ***/

		if (PEPA_ERR_OK != rc) {
			DE("Thread SHVA: An error happened in copy_between_sockects loop: %s\n", pepa_error_code_to_str(rc));
			pepa_back_to_disconnected_state();
			continue;
		}

		/* Else, if no critical error, we should reselt all and start over */

		/*** Ternimate IN thread ***/

		DDD("Thread SHVA: Going to stop the IN thread\n");
		const int in_calncel_rc = pthread_cancel(core->in_thread.thread_id);
		if (0 != in_calncel_rc) {
			DE("Thread SHVA: Error happened on stopping the IN thread\n");
		} else {
			DDD("Thread SHVA: The IN thread stopped\n");
		}


		/*** Terminate everything, start over ***/

		pepa_back_to_disconnected_state();
	} /* While (1) */


	DDD("Thread SHVA: Exiting the SHVA thread\n");
	pthread_exit(NULL);
}

void *pepa_ctl_thread(__attribute__((unused))void *arg)
{
	int         rc;
	pepa_core_t *core = pepa_get_core();

	/*** Init the thread ****/

	DDD("Thread CONTROL: Detaching\n");
	if (0 != pthread_detach(pthread_self())) {
		DE("Thread CONTROL: can't detach myself\n");
		perror("Thread CONTROL: can't detach myself");
		PEPA_TRY_ABORT();
		abort();
	}

	DDD("Thread CONTROL: Detached\n");

	rc = pthread_setname_np(pthread_self(), "CONTROL THREAD");
	if (0 != rc) {
		DE("Thread CONTROL: can't set name\n");
	}

	/* This is the main loop of this thread */

	while (1) {
		fd_set         read_set;
		int            max_fd;
		struct timeval tv;

		/* Socket sets */
		FD_ZERO(&read_set);

		FD_SET(core->controls.ctl_from_shva, &read_set);
		max_fd = PEPA_MAX(core->controls.ctl_from_shva, max_fd);

		FD_SET(core->controls.ctl_from_in, &read_set);
		max_fd = PEPA_MAX(core->controls.ctl_from_in, max_fd);

		FD_SET(core->controls.ctl_from_acceptor, &read_set);
		max_fd = core->controls.ctl_from_acceptor;

		FD_SET(core->controls.ctl_from_out, &read_set);
		max_fd = PEPA_MAX(core->controls.ctl_from_out, max_fd);

		rc = select(max_fd + 1, &read_set, NULL, NULL, &tv);

		/*** SHVA sent a signal ***/

		if (FD_ISSET(core->controls.ctl_from_shva, &read_set)) {
			/* Stop all threads but SHVA */
			DDD("Thread CONTROL: Event from SHVA\n");

			rc = pthread_cancel(core->acceptor_thread.thread_id);
			if (rc < 0) {
				DE("Thread CONTROL: Could not cancel ACCEPTOR thread\n");
			} else {
				DDD("Thread CONTROL: Terminated ACCEPTOR thread\n");
			}

			rc = pthread_cancel(core->out_thread.thread_id);
			if (rc < 0) {
				DE("Thread CONTROL: Could not cancel OUT thread\n");
			} else {
				DDD("Thread CONTROL: Terminated OUT thread\n");
			}

			rc = pthread_cancel(core->in_thread.thread_id);
			if (rc < 0) {
				DE("Thread CONTROL: Could not cancel IN thread\n");
			} else {
				DDD("Thread CONTROL: Terminated IN thread\n");
			}

			pepa_event_rm(core->controls.ctl_from_shva);
			DDD("Thread CONTROL: Removed even from SHVA\n");
		} /* if */

		/*** IN thread sent a signal ***/

		if (FD_ISSET(core->controls.ctl_from_in, &read_set)) {
			DDD("Thread CONTROL: Event from IN\n");
			/* We should kill all threads but SHVA, and send SHVA signal to start over */
			rc = pthread_cancel(core->acceptor_thread.thread_id);
			if (rc < 0) {
				DE("Could not cancel ACCEPTOR thread\n");
			} else {
				DDD("Thread CONTROL: Terminated ACCEPTOR thread\n");
			}

			rc = pthread_cancel(core->out_thread.thread_id);
			if (rc < 0) {
				DE("Could not cancel OUT thread\n");
			} else {
				DDD("Thread CONTROL: Terminated OUT thread\n");
			}
			/* Send signal to SHVA */
			pepa_event_send(core->controls.shva_from_ctl, 1);
			DDD("Thread CONTROL: Sent signal to SHVA\n");
			pepa_event_rm(core->controls.ctl_from_in);
			DDD("Thread CONTROL: Removed even from IN\n");
		} /* if */

		/*** ACCEPTOR thread sent a signal ***/

		if (FD_ISSET(core->controls.ctl_from_acceptor, &read_set)) {
			DDD("Thread CONTROL: Event from ACCEPTOR\n");
			/* We should kill all threads but SHVA, and send SHVA signal to start over */
			rc = pthread_cancel(core->in_thread.thread_id);
			if (rc < 0) {
				DE("Could not cancel IN thread\n");
			} else {
				DDD("Thread CONTROL: Terminated IN thread\n");
			}

			rc = pthread_cancel(core->out_thread.thread_id);
			if (rc < 0) {
				DE("Could not cancel OUT thread\n");
			} else {
				DDD("Thread CONTROL: Terminated OUT thread\n");
			}
			/* Send signal to SHVA */
			pepa_event_send(core->controls.shva_from_ctl, 1);
			DDD("Thread CONTROL: Sent signal to SHVA\n");

			pepa_event_rm(core->controls.ctl_from_acceptor);
			DDD("Thread CONTROL: Removed even from ACCEPTOR\n");
		} /* if */

		/*** OUT thread sent a signal ***/

		if (FD_ISSET(core->controls.ctl_from_out, &read_set)) {
			DDD("Thread CONTROL: Event from OUT\n");
			/* We should kill all threads but SHVA, and send SHVA signal to start over */
			rc = pthread_cancel(core->acceptor_thread.thread_id);
			if (rc < 0) {
				DE("Could not cancel ACCEPTOR thread\n");
			} else {
				DDD("Thread CONTROL: Terminated ACCEPTOR thread\n");
			}

			rc = pthread_cancel(core->in_thread.thread_id);
			if (rc < 0) {
				DE("Could not cancel IN thread\n");
			} else {
				DDD("Thread CONTROL: Terminated IN thread\n");
			}
			/* Send signal to SHVA */
			pepa_event_send(core->controls.shva_from_ctl, 1);
			DDD("Thread CONTROL: Sent signal to SHVA\n");
			pepa_event_rm(core->controls.ctl_from_out);
			DDD("Thread CONTROL: Removed even from OUT\n");
		} /* if */
	} /* While */
}

int pepa_start_threads(void)
{
	int         rc;
	pepa_core_t *core = pepa_get_core();

	/* Start CTL thread */
	DDD("Starting CTL thread\n");
	rc    = pthread_create(&core->ctl_thread.thread_id, NULL, pepa_ctl_thread, NULL);
	if (0 == rc) {
		DDD("CTL thread is started\n");
	} else {
		pepa_print_pthread_create_error(rc);
		return -1;
	}

	/* Start SHVA thread */
	DDD("Starting SHVA thread\n");
	rc    = pthread_create(&core->shva_thread.thread_id, NULL, pepa_shva_thread, NULL);
	if (0 == rc) {
		DDD("SHVA thread is started\n");
	} else {
		pepa_print_pthread_create_error(rc);
		return -1;
	}

//	sleep(3);
//	pepa_event_send(core->controls.ctl_from_shva, 1);

#if 0 /* SEB */
	usleep(500);

	/* For debug only */
	DDD("Starting IN thread\n");
	rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_in_thread, NULL);

	if (0 == rc) {
		DDD("IN thread is started\n");
	} else {
		pepa_print_pthread_create_error(rc);
		return -1;
	}
#endif

	return rc;
}

int pepa_stop_threads(void)
{
	/* TODO */
	return -1;
}

