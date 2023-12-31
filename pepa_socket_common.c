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
	slog_note_l("Thread %s: Detaching", name);
	if (0 != pthread_detach(pthread_self())) {
		slog_fatal_l("Thread %s: can't detach myself", name);
		perror("Thread : can't detach myself");
		return -PEPA_ERR_THREAD_DETOUCH;
	}

	slog_note_l("Thread %s: Detached", name);
	slog_note("####################################################");

	int rc = pthread_setname_np(pthread_self(), name);
	if (0 != rc) {
		slog_fatal_l("Thread %s: can't set name", name);
	}

	return PEPA_ERR_OK;
}

void pepa_parse_pthread_create_error(const int rc)
{
	switch (rc) {
	case EAGAIN:
		slog_fatal_l("Insufficient resources to create another thread");
		break;
	case EINVAL:
		slog_fatal_l("Invalid settings in attr");
		break;
	case EPERM:
		slog_fatal_l("No permission to set the scheduling policy and parameters specified in attr");
		break;
	default:
		slog_fatal_l("You should never see this message: error code %d", rc);
	}
}

void pepa_set_tcp_timeout(int sock)
{
//	return;
	struct timeval time_out;
	time_out.tv_sec = 1;
	time_out.tv_usec = 0;

	if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out))) {
		slog_debug_l("[from %s] SO_RCVTIMEO has a problem", "EMU SHVA", strerror(errno));
	}
	if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&time_out, sizeof(time_out))) {
		slog_debug_l("[from %s] SO_SNDTIMEO has a problem", "EMU SHVA", strerror(errno));
	}
}

void pepa_set_tcp_recv_size(pepa_core_t *core, int sock)
{
//	return;
	int buf_size = core->internal_buf_size * 1024;

	/* Set TCP receive window size */
	if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&buf_size, sizeof(buf_size))) {
		slog_debug_l("[from %s] SO_RCVBUF has a problem", "EMU SHVA", strerror(errno));
	}
}

void pepa_set_tcp_send_size(pepa_core_t *core, int sock)
{
//	return;
	int buf_size = core->internal_buf_size * 1024;
	/* Set TCP sent window size */
	if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&buf_size, sizeof(buf_size))) {
		slog_debug_l("[from %s] SO_SNDBUF has a problem", "EMU SHVA", strerror(errno));
	}
}

void pepa_set_tcp_connection_props(pepa_core_t *core, int sock)
{
	struct timeval time_out;
	time_out.tv_sec = 10;
	time_out.tv_usec = 0;

	int buf_size = core->internal_buf_size * 1024;

	if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out))) {
		slog_debug_l("[from %s] tsetsockopt function has a problem", "EMU SHVA", strerror(errno));
	}
	if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&time_out, sizeof(time_out))) {
		slog_debug_l("[from %s] tsetsockopt function has a problem", "EMU SHVA", strerror(errno));
	}

	/* Set TCP receive window size */
	if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&buf_size, sizeof(buf_size))) {
		slog_debug_l("[from %s] tsetsockopt function has a problem", "EMU SHVA", strerror(errno));
	}

	/* Set TCP sent window size */
	if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&buf_size, sizeof(buf_size))) {
		slog_debug_l("[from %s] tsetsockopt function has a problem", "EMU SHVA", strerror(errno));
	}
}


int pepa_one_direction_copy2(int fd_out, const char *name_out,
							 int fd_in, const char *name_in,
							 char *buf, size_t buf_size, int do_debug,
							 uint64_t *ext_rx, uint64_t *ext_tx)
{
	int ret       = PEPA_ERR_OK;
	int rx        = 0;
	int tx_total  = 0;
	int rx_total  = 0;
	int iteration = 0;

	if (do_debug) {
		// slog_note_l("Starrting transfering from %s to %s", name_in, name_out);
	}

	do {
		int tx         = 0;
		int tx_current = 0;

		iteration++;
		if (do_debug) {
			// slog_note_l("Iteration: %d", iteration);
		}

		/* Read one time, then we will transfer it possibly in pieces */
		rx = read(fd_in, buf, buf_size);

		if (do_debug) {
			// slog_note_l("Iteration: %d, finised read(), there is %d bytes", iteration, rx);
		}

		if (rx < 0) {
			if (do_debug) {
				slog_error_l("Could not read: from read sock %s [%d]: %s", name_in, fd_in, strerror(errno));
			}
			ret = -PEPA_ERR_BAD_SOCKET_READ;
			goto endit;
		}

		/* nothing to read*/
		if ((0 == rx) && (1 == iteration)) {
			/* If we can not read on the first iteration, it probably means the fd was closed */
			ret = -PEPA_ERR_BAD_SOCKET_READ;

			if (do_debug) {
				slog_error_l("Could not read on the first iteration: from read sock %s [%d] out socket %s [%d}: %s",
							 name_in, fd_in, name_out, fd_out, strerror(errno));
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
					slog_warn_l("Could not write to write to sock %s [%d]: returned -1: %s", name_out, fd_out, strerror(errno));
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
			// slog_note_l("Iteration %d done: rx = %d, tx = %d", iteration, rx, tx);
		}

	} while ((int)buf_size == rx); /* Tun this loop as long as we have data on read socket */

	ret = PEPA_ERR_OK;
endit:
	if (do_debug) {
		// slog_note_l("Finished transfering from %s to %s, returning %d, rx = %d, tx = %d, ", name_in, name_out, ret, rx_total, tx_total);
	}

	if (rx_total > 0) {
		*ext_rx += rx_total;

	}

	if (tx_total > 0) {
		*ext_tx += tx_total;

	}
	return ret;
}

int pepa_test_fd(int fd)
{
	if ((fcntl(fd, F_GETFL) < 0) && (EBADF == errno)) {
		return -PEPA_ERR_FILE_DESCRIPTOR;
	}
	return PEPA_ERR_OK;
}

int epoll_ctl_add(int epfd, int fd, uint32_t events)
{
	struct epoll_event ev;

	if (fd < 0) {
		slog_fatal_l("Tryed to add fd < 0: %d", fd);
		return -PEPA_ERR_FILE_DESCRIPTOR;
	}

	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		slog_fatal_l("Can not add fd %d to epoll: %s", fd, strerror(errno));
		return -PEPA_ERR_EPOLL_CANNOT_ADD;
	}

	return PEPA_ERR_OK;
}

int pepa_socket_shutdown_and_close(int sock, const char *my_name)
{
	if (sock < 0) {
		slog_warn_l("%s: Looks like the socket is closed: == %d", my_name);
		return -PEPA_ERR_FILE_DESCRIPTOR;
	}

	int rc = shutdown(sock, SHUT_RDWR);
	if (rc < 0) {
		slog_warn_l("%s: Could not shutdown the socket: fd: %d, %s", my_name, sock, strerror(errno));
		return -PEPA_ERR_FILE_DESCRIPTOR;
	}

	rc = close(sock);
	if (rc < 0) {
		slog_warn_l("%s: Could not close the socket: fd: %d, %s", my_name, sock, strerror(errno));
		return -PEPA_ERR_CANNOT_CLOSE;
	}

	close(sock);
	slog_note_l("%s: Closed socket %d", my_name, sock);
	return PEPA_ERR_OK;
}

void pepa_socket_close(int fd, const char *socket_name)
{
	if (fd < 0) {
		slog_error_l("Can not close socket %s, its value is %d", socket_name, fd);
		return;
	}

	int rc = close(fd);
	if (0 != rc) {
		slog_error_l("Can not close socket %s, error %d:%s", socket_name, rc, strerror(errno));
		return;
	}

	slog_note_l("## Closed socket socket %s");
}

void pepa_socket_close_shva_rw(pepa_core_t *core)
{
	close(core->sockets.shva_rw);
	core->sockets.shva_rw = -1;
	slog_note_l("Closed core->sockets.shva_rw");
}

void pepa_socket_close_out_write(pepa_core_t *core)
{
	close(core->sockets.out_write);
	core->sockets.out_write = -1;
	slog_note_l("Closed core->sockets.out_write");
}

void pepa_socket_close_out_listen(pepa_core_t *core)
{
	pepa_socket_close_out_write(core);
	if (PEPA_ERR_OK != pepa_socket_shutdown_and_close(core->sockets.out_listen, "OUT LISTEN")) {
		slog_debug_l("Close and shutdown of the OUT LISTEN is failed");
	}
	core->sockets.out_listen = -1;
	slog_note_l("Closed core->sockets.out_listen");
}

void pepa_socket_close_in_listen(pepa_core_t *core)
{
	if (PEPA_ERR_OK != pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN")) {
		slog_debug_l("Close and shutdown od IN LISTEN is failed");
	}
	core->sockets.in_listen = -1;
	slog_note_l("Closed core->sockets.in_listen");
}

__attribute__((nonnull(1, 2)))
int pepa_open_listening_socket(struct sockaddr_in *s_addr,
							   const buf_t *ip_address,
							   const int port,
							   const int num_of_clients,
							   const char *name)
{
	int rc   = PEPA_ERR_OK;
	int sock;

	//slog_debug_l("Open Socket [from %s]: starting for %s:%d, num of clients: %d", name, ip_address->data, port, num_of_clients);

	memset(s_addr, 0, sizeof(struct sockaddr_in));
	s_addr->sin_family = (sa_family_t)AF_INET;
	s_addr->sin_port = htons(port);

	//slog_debug_l("Open Socket [from %s]: Port is %d", name, port);

	const int inet_aton_rc = inet_aton(ip_address->data, &s_addr->sin_addr);

	//slog_note_l("Open Socket [from %s]: specified address, use %s", name, ip_address->data);
	/* Warning: inet_aton() returns 1 on success */
	if (1 != inet_aton_rc) {
		slog_fatal_l("Open Socket [from %s]: Could not convert string address to in_addr_t: %s", name, strerror(errno));
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	// slog_note_l("Open Socket [from %s]: Going to create socket for %s:%d", name, ip_address->data, port);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock  < 0) {
		slog_error_l("Open Socket [from %s]: Could not create listen socket: %s", name, strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}

#if 0 /* SEB */
	struct timeval time_out;
	time_out.tv_sec = 1;
	time_out.tv_usec = 0;

	if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out))) {
		slog_debug_l("[from %s] tsetsockopt function has a problem", name, strerror(errno));
	}

	if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&time_out, sizeof(time_out))) {
		slog_debug_l("[from %s] tsetsockopt function has a problem", name, strerror(errno));
	}
#endif
	const int enable = 1;
	rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (rc < 0) {
		slog_error_l("Open Socket [from %s]: Could not set SO_REUSEADDR on socket, error: %s", name, strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	// pepa_set_tcp_connection_props(core, sock);

	//do {
	rc = bind(sock, (struct sockaddr *)s_addr, (socklen_t)sizeof(struct sockaddr_in));
	if (rc < 0 && EADDRINUSE == errno) {
		slog_warn_l("Address in use, can't bind, [from %s], waiting...", name);
		// sleep(10);
		return -PEPA_ERR_SOCKET_BIND;
	}
	//} while (rc < 0 && EADDRINUSE == errno);

	if (rc < 0) {
		slog_error_l("Open Socket [from %s]: Can't bind: %s", name, strerror(errno));
		close(sock);
		return (-PEPA_ERR_SOCKET_IN_USE);
	}

	rc = listen(sock, num_of_clients);
	if (rc < 0) {
		slog_fatal_l("Open Socket [from %s]: Could not set SERVER_CLIENTS: %s", name, strerror(errno));
		close(sock);
		return (-PEPA_ERR_SOCKET_LISTEN);
	}

	//slog_note_l("Open Socket [from %s]: Opened listening socket: %d", name, sock);
	return (sock);
}

int pepa_open_connection_to_server(const char *address, int port, const char *name)
{
	struct sockaddr_in s_addr;
	int                sock;

	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = (sa_family_t)AF_INET;

	slog_note_l("[from %s]: Converting addr string to binary: |%s|", name, address);
	const int convert_rc = inet_pton(AF_INET, address, &s_addr.sin_addr);
	if (0 == convert_rc) {
		slog_fatal_l("[from %s]: The string is not a valid IP address: |%s|", name, address);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	if (convert_rc < 0) {
		slog_fatal_l("[from %s]: Could not convert string addredd |%s| to binary", name, address);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	s_addr.sin_port = htons(port);

	slog_note_l("[from %s]: Creating socket: |%s|", name, address);
	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		slog_fatal_l("[from %s]: Could not create socket", name);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCKET_CREATION);
	}

#if 0 /* SEB */
	struct timeval time_out;
	time_out.tv_sec = 1;
	time_out.tv_usec = 0;
	if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out))) {
		slog_debug_l("[from %s] tsetsockopt function has a problem", name, strerror(errno));
	}
	if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&time_out, sizeof(time_out))) {
		slog_debug_l("[from %s] tsetsockopt function has a problem", name, strerror(errno));
	}

#endif
	slog_note_l("[from %s]: Starting connect(): |%s|", name, address);
	if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
		slog_debug_l("[from %s]: Could not connect to server: %s", name, strerror(errno));
		close(sock);
		//PEPA_TRY_ABORT();
		return (-PEPA_ERR_SOCK_CONNECT);
	}
	//pepa_set_tcp_connection_props(core, sock);

	slog_note_l("[from %s]: Opened connection to server: %d", name, sock);

	return (sock);
}



