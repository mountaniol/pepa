#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h> /* For read() */

#include <errno.h>
#include "pepa_socket.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "buf_t/buf_t.h"
#include "buf_t/se_debug.h"

#ifndef PPEPA_MAX
	#define PEPA_MAX(a,b) ( a > b ? a : b)
#endif

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
		return (NULL);
	}

	xconn->buf = buf_new(buf_size);
	if (NULL == xconn->buf) {
		DE("Can not allocate buf_t\n");
		free(xconn);
		return NULL;
	}

	xconn->fd_src = fd_src;
	xconn->fd_dst = fd_dst;

	return (xconn);
}

static void x_connect_t_release(x_connect_t *xconn)
{
	if (NULL == xconn) {
		DE("Arg is NULL\n");
		return;
	}

	const int rc = buf_free(xconn->buf);
	if (BUFT_OK != rc) {
		DE("Can not release buf_t object\n");
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
static int pepa_open_socket(struct sockaddr_in *s_addr, buf_t *ip_address, int port, int num_of_clients)
{
	int rc   = PEPA_ERR_OK;
	int sock;

	if (NULL == s_addr) {
		syslog(LOG_ERR, "Can't allocate socket: %s\n", strerror(errno));
		perror("Can't allocate socket");
		return (-PEPA_ERR_NULL_POINTER);
	}

	memset(s_addr, 0, sizeof(struct sockaddr_in));
	s_addr->sin_family = (sa_family_t)AF_INET;
	s_addr->sin_port = htons(port);

	if (NULL == ip_address || NULL == ip_address->data) {
		s_addr->sin_addr.s_addr = htonl(INADDR_ANY);
	} else {
		//s_addr->sin_addr.s_addr = inet_addr(ip_address->data);
		const int inet_aton_rc = inet_aton(ip_address->data, &s_addr->sin_addr);

		/* Warning: inet_aton() returns 1 on success */
		if (1 != inet_aton_rc) {
			DE("Could not convert string address to in_addr_t\n");
			return (-PEPA_ERR_CONVERT_ADDR);
		}
	}

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock  < 0) {
		syslog(LOG_ERR, "could not create listen socket: %s\n", strerror(errno));
		perror("could not create listen socket");
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	rc = bind(sock, (struct sockaddr *)s_addr, (socklen_t)sizeof(struct sockaddr_in));
	if (rc < 0) {
		syslog(LOG_ERR, "Can't bind: %s\n", strerror(errno));
		perror("Can't bind");
		close(sock);
		return (-PEPA_ERR_SOCKET_BIND);
	}

	rc = listen(sock, num_of_clients);
	if (rc < 0) {
		syslog(LOG_ERR, "could not set SERVER_CLIENTS: %s\n", strerror(errno));
		perror("could not set SERVER_CLIENTS");
		close(sock);
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
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	DD("2\n");

	if (convert_rc < 0) {
		DE("Could not convert string addredd |%s| to binary\n", core->shva_thread.ip_string->data);
		return (-PEPA_ERR_CONVERT_ADDR);
	}
	DD("3\n");

	// s_addr.sin_port = htons(ip->port);
	s_addr.sin_port = htons(core->shva_thread.port);

	DD("4\n");

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		DE("could not create socket\n");
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	DD("5\n");

	if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
		DE("could not connect to server\n");
		close(sock);
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
static int pepa_analyse_accept_error(int error_code)
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
static int pepa_copy_between_sockects_execute(x_connect_t *xcon, x_conn_direction_t direction)
{
	ssize_t rs;

	int     fd_from;
	int     fd_to;

	if (X_CONN_COPY_LEFT == direction) {
		fd_from = xcon->fd_dst;
		fd_to = xcon->fd_src;
	} else {
		fd_from = xcon->fd_src;
		fd_to = xcon->fd_dst;

	}

	xcon->buf->used = read(fd_from, xcon->buf->data, xcon->buf->room);

	if (xcon->buf->used < 0) {
		DE("Error on reading from socket\n");
		return (-PEPA_ERR_SOCKET_READ);
	}

	if (0 == xcon->buf->used) {
		DE("Probably closed socket 'from'\n");
		return (-PEPA_ERR_SOCKET_READ_CLOSED);
	}

	rs = write(fd_to, xcon->buf->data, xcon->buf->used);

	if (rs < 0) {
		DE("Error on writing to socket\n");
		return (-PEPA_ERR_SOCKET_WRITE);
	}

	if (0 == rs) {
		DE("Probably closed file descriptor od 'write' socket:\n");
		return (-PEPA_ERR_SOCKET_WRITE_CLOSED);
	}

	return (PEPA_ERR_OK);
}

/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief This function takes two file descriptors (of sockets),
 *  	  and passes buffers between them as a tunnel
 * @param int fd_left Socket file descriptor
 * @param int fd_right Socket file descriptor
 * @return int Status of an error. This function suppposed to
 *  	   run in infinite loop. If it returned, it signalizes
 *  	   the reason of the return.
 * @details 
 */
static int pepa_copy_between_sockects(int fd_src, int fd_dst)
{
	/* We need two buffer:  */
	int         rc;

	/* Allocate x_connect_t struct */
	/* TODO: The size of the buffer should be configurable */
	x_connect_t *xconn = x_connect_t_alloc(fd_src, fd_dst, X_BUF_SIZE);
	TESTP(xconn, -PEPA_ERR_ALLOCATION);

	while (1) {
		fd_set         read_set;
		fd_set         ex_set;
		struct timeval tv;


		tv.tv_sec = 0;
		tv.tv_usec = 5;

		/* Socket sets */
		FD_ZERO(&read_set);
		FD_ZERO(&ex_set);

		/* Read set, signaling when there is data to read */
		FD_SET(fd_src, &read_set);

		rc = select(fd_src + 1, &read_set, NULL, NULL, &tv);

		/* Data is ready on the src file descriptor:
		   read it from the left fd, write to the right: the RIGHT direction */
		if (FD_ISSET(fd_src, &read_set)) {
			rc = pepa_copy_between_sockects_execute(xconn, X_CONN_COPY_RIGHT);
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
	pepa_in_thread_fds_t *fds = malloc(sizeof(pepa_in_thread_fds_t));
	if (NULL == fds) {
		DE("Can't allocate\n");
		return (NULL);
	}

	memset(fds, 0, sizeof(pepa_in_thread_fds_t));
	fds->buf_fds = buf_array(sizeof(int), 1024);
	if (NULL == fds->buf_fds) {
		DE("Can not allocate buf_t\n");
		free(fds);
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
		return (NULL);
	}

	return (fds);
}

static void pepa_in_thread_fds_t_release(pepa_in_thread_fds_t *fds)
{
	if (NULL == fds) {
		DE("Arg is NULL\n");
		return;
	}

	int rc = sem_destroy(&fds->buf_fds_mutex);
	if (0 != rc) {
		DE("Could not destroy mutex\n");
		perror("sem destroy failure: ");
		return;
	}

	if (fds->buf_fds) {
		rc = buf_free(fds->buf_fds);
		if (BUFT_OK != rc) {
			DE("Could not destroy buf_t, memory leas is possible\n");
		}
	}

	/* Secure way: clear memory before release it */
	memset(fds, 0, sizeof(pepa_in_thread_fds_t));
	free(fds);
}

int pepa_buf_fds_find_max(buf_t *buf)
{
	int max         = 0;
	int index;
	int buf_members = buf_arr_get_members_count(buf);

	if (buf_members < 0) {
		DE("Can not get count of buf array\n");
		return -1;
	}

	for (index = 0; index < buf_members; index++) {
		int *mem_ptr = buf_arr_get_member_ptr(buf, index);
		if (*mem_ptr > max) {
			max = *mem_ptr;
		}
	}

	return max;
}

static void set_buf_flag(pepa_in_thread_fds_t *fds, int val)
{
	sem_wait(&fds->buf_fds_mutex);
	fds->buf_changed_flag = val;
	sem_post(&fds->buf_fds_mutex);
}

/**
 * @author Sebastian Mountaniol (12/9/23)
 * @brief This thrad sitting on accept(), ceate a new sockets,
 *  	  and adds them to the array.
 * @param void* arg   
 * @return void* 
 * @details 
 */
void *pepa_in_thread_acceptor(void *arg)
{
	pepa_in_thread_fds_t *fds         = (pepa_in_thread_fds_t *)arg;
	while (1) {
		struct sockaddr addr;
		socklen_t       addrlen = sizeof(struct sockaddr);
		int             fd      = accept(fds->socket, (struct sockaddr *)&addr, &addrlen);

		/* If something went wrong, analyze the error and decide what to do */
		if (fd < 0) {
			DE("Could not accept incoming connection\n");
			continue;
		}

		sem_wait(&fds->buf_fds_mutex);
		int rc = buf_arr_add(fds->buf_fds, &fd);
		fds->buf_changed_flag++;
		sem_post(&fds->buf_fds_mutex);
		if (BUFT_OK != rc) {
			DE("Could not set new member in buffer array\n");
			abort();
		}
	} /* while (1) */
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
	struct sockaddr_in   s_addr;
	pepa_in_thread_fds_t *fds            = pepa_in_thread_fds_t_alloc();
	pthread_t            acceptor_thread;
	x_connect_t          *xconn          = NULL;

	if (0 != pthread_detach(pthread_self())) {
		DE("Thread: can't detach myself\n");
		perror("Thread: can't detach myself");
		abort();
	}

	pepa_core_t *core     = pepa_get_core();

	/* TODO: Make it configurable */
	fds->socket = pepa_open_socket(&s_addr, core->in_thread.ip_string, core->in_thread.port, 1024);
	if (fds->socket < 0) {
		DE("Can not create SHVA socket\n");
		pthread_exit(NULL);
	}

	/* Create an additional thread to accept new connections */
	pthread_create(&acceptor_thread, NULL, pepa_in_thread_acceptor, (void *)fds);

	/* Now run the loop until we have the first IN socket connected  */

	while (0 == fds->buf_changed_flag) {
		usleep(10);
	}

	xconn = x_connect_t_alloc(core->shva_thread.fd, 0, X_BUF_SIZE);

	/* Here: we have at least one IN socket connected */
	while (1) {
		int            index;
		int            fd_members;
		int            fd_max     = 0;
		fd_set         read_set;
		struct timeval tv;

		tv.tv_sec = 0;
		tv.tv_usec = 5;

		FD_ZERO(&read_set);

		/* TODO: Text error */
		/* Reset 'new file descriptor added' flag */
		set_buf_flag(fds, 0);

		/* Sockets set */
		fd_members = buf_arr_get_members_count(fds->buf_fds);
		for (index = 0; fd_members; index++) {
			int *fd = buf_arr_get_member_ptr(fds->buf_fds, index);
			FD_SET(*fd, &read_set);
			if (fd_max < *fd) {
				fd_max = *fd;
			}
		}

		/* Now run in this loop until a file descriptor not changed */
		do {
			int rc = select(fd_max + 1, &read_set, NULL, NULL, &tv);

			/* If returned 0 means no change in file descriptors; if it is EINTR it means we interrupted by a signal */
			if (0 == rc || EINTR == rc) {
				continue;
			}

			fd_members = buf_arr_get_members_count(fds->buf_fds);

			for (index = 0; index < fd_members; index++) {
				int *fd = buf_arr_get_member_ptr(fds->buf_fds, index);

				/* If current file descriptor was not changed, continue */
				if (!FD_ISSET(*fd, &read_set)) {
					continue;
				} /* if (FD_ISSET(*fd, &read_set) */

				/* File descriptor was changed, we should transfer buffer to SHVA */
				xconn->fd_src = *fd;

				int rc = pepa_copy_between_sockects_execute(xconn, X_CONN_COPY_RIGHT);
				if (PEPA_ERR_OK == rc) {
					continue;
				}

				/* Can not read from the file descriptor; probably it was closed */
				if (PEPA_ERR_SOCKET_READ_CLOSED == rc) {
					/* Remove the socket */
					sem_wait(&fds->buf_fds_mutex);
					rc = buf_arr_rm(fds->buf_fds, index);
					/* And close the socket file descriptor */
					close(*fd);
					fds->buf_changed_flag++;
					sem_post(&fds->buf_fds_mutex);

					if (BUFT_OK != rc) {
						DE("Could not remove member from the buf array\n");
						abort();
					}
				}
			} /* for (index = 0; fd_members; index++) */
			/* Data is ready on the src file descriptor:
			   read it from the left fd, write to the right: the RIGHT direction */

		} while (fds->buf_changed_flag < 1);
	}

	return NULL;
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
void *pepa_shva_socket_thread(__attribute__((unused))void *arg)
{
	struct sockaddr_in s_addr;

	if (0 != pthread_detach(pthread_self())) {
		DE("Thread: can't detach myself\n");
		perror("Thread: can't detach myself");
		abort();
	}

	pepa_core_t *core   = pepa_get_core();

	/* When this thread started, the 'out' socket should be opened already */
	if (core->out_thread.fd < 0) {
		DE("OUT socket is not acive - terminating SHVA thread\n");
		pthread_exit(NULL);
	}

	/* 1. Create OUT socket */

	const int   out_socket = pepa_open_socket(&s_addr, core->out_thread.ip_string, core->out_thread.port, 1);
	if (out_socket < 0) {
		DE("Can not create SHVA socket\n");
		pthread_exit(NULL);
	}

	/* 2. Wait until the OUT is connected */

	/* Acept OUT connection */
	core->out_thread.fd = -1;

	/* This is the main loop of this thread */

	while (1) {
		while (core->out_thread.fd < 0) {
			int       error_action = PEPA_ERR_OK;

			socklen_t addrlen      = sizeof(struct sockaddr);
			core->out_thread.fd = accept(out_socket, (struct sockaddr *)&s_addr, &addrlen);

			/* If something went wrong, analyze the error and decide what to do */
			if (core->out_thread.fd < 0) {
				error_action = pepa_analyse_accept_error(errno);
			}

			if (error_action < 0) {
				pthread_exit(NULL);
			}
		}

		/* 4. Connect to SHVA server */
		core->shva_thread.fd = pepa_open_shava_connection();

		if (core->shva_thread.fd < 0) {
			DE("Can not open connection to SHVA server\n");
			/* Here we shoudl close connection to OUT socket and wait for the new OUT connection */
			close(core->out_thread.fd);
			core->out_thread.fd = -1;
			continue;
		}

		/* Here we can start transfer SHVA -> OUT; before we do it, we start IN thread */
		pthread_create(&core->in_thread.thread_id, NULL, pepa_in_thread, NULL);

		int rc = pepa_copy_between_sockects(core->shva_thread.fd, core->out_thread.fd);

		if (PEPA_ERR_OK != rc) {
			DE("An error heppened: %s\n", pepa_error_code_to_str(rc));
			break;
		}

		/* Else, if no critical error, we should reselt all and start over */

		/* Ternimate IN thread */
		pthread_cancel(core->in_thread.thread_id);

		/* TODO: Close IN descriptors ??? */

		/* Terminate SHVA connection */
		close(core->shva_thread.fd);
		core->shva_thread.fd = -1;

		/* Terminate OUT connection */
		close(core->out_thread.fd);
		core->out_thread.fd = -1;
	} /* While (1) */

	pthread_exit(NULL);
}

