#define _GNU_SOURCE 
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "slog/src/slog.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_state_machine.h"

enum pepa3_go_states {
	PST_START = 1000,
	PST_CLOSE_SOCKETS,
	PST_WAIT_OUT,
	PST_OPEN_SHVA,
	PST_START_IN,
	PST_TRANSFER_LOOP,
	PST_END,
};

enum pepa3_transfer_states {
	TR_IN_CONNECTION = 2000,
	TR_SHVA_READ,
	TR_SHVA_IN_READ,
};

enum pepa3_errors {
	TE_RESTART = 3000,
	TE_IN_RESTART,
	TE_IN_REMOVED,
};

#define EVENTS_NUM (100)
#define EPOLL_TIMEOUT (100)

#define EMPTY_SLOT (-1)

static void pepa_in_reading_sockets_close_all(pepa_core_t *core)
{
	int i;
	slog_note_l("Starting closing and removing sockets: %d slots", core->in_reading_sockets.number);
	for (i = 0; i < core->in_reading_sockets.number; i++) {
		if (EMPTY_SLOT != core->in_reading_sockets.sockets[i]) {

			//close(core->in_reading_sockets.sockets[i]);
			slog_note_l("Going to close in reading socket %d port %d",
						core->in_reading_sockets.sockets[i],
						pepa_find_socket_port(core->in_reading_sockets.sockets[i]));

			int rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->in_reading_sockets.sockets[i], NULL);

			if (rc_remove) {
				slog_warn_l("Could not remove socket %d from epoll set", core->in_reading_sockets.sockets[i]);
			}

			pepa_reading_socket_close(core->in_reading_sockets.sockets[i], "IN FORWARD READ");
			slog_note_l("Closed socket %d in slot %d", core->in_reading_sockets.sockets[i], i);
			core->in_reading_sockets.sockets[i] = EMPTY_SLOT;
		}
	}
	slog_note_l("Finished closing and removing sockets: %d slots", core->in_reading_sockets.number);
}

static void pepa_in_reading_sockets_free(pepa_core_t *core)
{
	slog_note_l("Starting socket closing and cleaning");
	pepa_in_reading_sockets_close_all(core);
	free(core->in_reading_sockets.sockets);
	core->in_reading_sockets.sockets = NULL;
	slog_note_l("Finished socket closing and cleaning");
}

static void pepa_in_reading_sockets_allocate(pepa_core_t *core, int num)
{
	int i;
	core->in_reading_sockets.number = num;
	core->in_reading_sockets.sockets = malloc(sizeof(int) * num);
	for (i = 0; i < num; i++) {
		core->in_reading_sockets.sockets[i] = EMPTY_SLOT;
	}
	slog_note_l("Allocated %d socket slots", num);
}

static void pepa_in_reading_sockets_add(pepa_core_t *core, int fd)
{
	int i;
	for (i = 0; i < core->in_reading_sockets.number; i++) {
		if (EMPTY_SLOT == core->in_reading_sockets.sockets[i]) {
			core->in_reading_sockets.sockets[i] = fd;
			slog_note_l("Added socket %d to slot %d", core->in_reading_sockets.sockets[i], i);
			return;
		}
	}
}

static void pepa_in_reading_sockets_close_rm(pepa_core_t *core, int fd)
{
	int i;
	for (i = 0; i < core->in_reading_sockets.number; i++) {
		if (fd == core->in_reading_sockets.sockets[i]) {
			slog_note_l("Going to close in reading socket %d port %d",
						core->in_reading_sockets.sockets[i],
						pepa_find_socket_port(core->in_reading_sockets.sockets[i]));

			pepa_reading_socket_close(core->in_reading_sockets.sockets[i], "IN-FORWARD");
			slog_debug_l("Closed and removed socket %d", core->in_reading_sockets.sockets[i]);
			core->in_reading_sockets.sockets[i] = EMPTY_SLOT;
		}
	}
}

int pepa_process_exceptions(pepa_core_t *core, struct epoll_event events[], int event_count)
{
	int ret       = PEPA_ERR_OK;
	int rc_remove;
	int i;
	for (i = 0; i < event_count; i++) {
		if (!(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
			//if (!(events[i].events & (EPOLLRDHUP | EPOLLHUP))) {
			continue;
		}

		/*** The remote side is disconnected ***/

		/* If one of the read/write sockets is diconnected, exit the thread */
		if (events[i].events & (EPOLLRDHUP)) {

			/* SHVA reading socket is disconnected */
			if (core->sockets.shva_rw == events[i].data.fd) {
				slog_warn_l("SHVA socket: remote side of the socket is disconnected");
				return TE_RESTART;
			}

			/* OUT writing socket is disconnected */
			if (core->sockets.out_write == events[i].data.fd) {
				slog_warn_l("OUT socket: remote side of the OUT write socket is disconnected");
				return TE_RESTART;
			}

			/* OUT listener socket is disconnected */
			if (core->sockets.out_listen == events[i].data.fd) {
				slog_warn_l("OUT socket: remote side of the OUT listen is disconnected");
				return TE_RESTART;
			}

			/* Else: it is one of IN reading sockets, we should remove it */
			rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
			if (rc_remove) {
				slog_warn_l("Could not remove socket %d from epoll set", events[i].data.fd);
			}
			pepa_in_reading_sockets_close_rm(core, events[i].data.fd);
			ret = TE_IN_REMOVED;

		} /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) */

		/*** This side is broken ***/

		if (events[i].events & (EPOLLHUP)) {

			/* SHVA reading socket is disconnected */
			if (core->sockets.shva_rw == events[i].data.fd) {
				slog_warn_l("SHVA socket: local side of the socket is broken");
				return TE_RESTART;
			}

			/* OUT writing socket is disconnected */
			if (core->sockets.out_write == events[i].data.fd) {
				slog_warn_l("OUT socket: local side of the OUT write socket is broken");
				return TE_RESTART;
			}

			/* OUT listener socket is disconnected */
			if (core->sockets.out_listen == events[i].data.fd) {
				slog_warn_l("OUT socket: local side of the OUT listen is broken");
				return TE_RESTART;
			}

			/* IN listener socket is degraded */
			if (core->sockets.in_listen == events[i].data.fd) {
				slog_warn_l("IN socket: local side of the IN listen is broken");
				return TE_IN_RESTART;
			}

			/* Else: it is one of IN reading sockets, we should remove it */
			rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
			if (rc_remove) {
				slog_warn_l("Could not remove socket %d from epoll set", events[i].data.fd);
			}
			pepa_in_reading_sockets_close_rm(core, events[i].data.fd);
			ret = TE_IN_REMOVED;

		} /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) */
	}
	return ret;
}

/**
 * @author Sebastian Mountaniol (1/7/24)
 * @brief THis function is called when IN socket should accept a
 *  	  new connection. This function calls accept() and adds
 *  	  a new socket into epoll set, and into the array of IN
 *  	  reading sockets
 * @param pepa_core_t* core  Pepa core structure 
 * @return int32_t PEPA_ERR_OK on success; an error if accept failed
 * @details 
 */
static int32_t pepa_in_accept_new_connection(pepa_core_t *core)
{
	struct sockaddr_in address;
	int32_t            new_socket = -1;
	static int32_t     addrlen    = sizeof(address);

	if ((new_socket = accept(core->sockets.in_listen,
							 (struct sockaddr *)&address,
							 (socklen_t *)&addrlen)) < 0) {
		slog_error_l("Error on accept: %s", strerror(errno));
		return -1;
	}

	// pepa_set_tcp_connection_props(core, new_socket);
	pepa_set_tcp_timeout(new_socket);
	pepa_set_tcp_recv_size(core, new_socket);
	const int enable = 1;
	int       rc     = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (rc < 0) {
		slog_error_l("Open Socket: Could not set SO_REUSEADDR on socket, error: %s", strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	if (0 != epoll_ctl_add(core->epoll_fd, new_socket, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		slog_error_l("Can not add new socket to epoll set: %s", strerror(errno));
		pepa_reading_socket_close(new_socket, "IN FORWARD-READ");
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	/* Add to the array of IN reading sockets */
	pepa_in_reading_sockets_add(core, new_socket);

	slog_warn_l("Added new socket %d to epoll set", new_socket);
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (1/7/24)
 * @brief Process waiting signals on all sockets 
 * @param pepa_core_t* core       Core structure 
 * @param struct epoll_event[] events     Events from epoll
 * @param int event_count Number of events
 * @return int Returns PEPA_ERR_OK if all processed and no
 *  	   errors.Else returns a status of state machine which
 *  	   requres to reset all sockets. 
 * @details 
 */
static int pepa_process_fdx(pepa_core_t *core, struct epoll_event events[], int event_count)
{
	int32_t rc             = PEPA_ERR_OK;
	int32_t i;

	int     fd_read        = -1;
	int     fd_write       = -1;
	char    *fd_name_read  = NULL;
	char    *fd_name_write = NULL;
	uint64_t *read_stat;
	uint64_t *write_stat;

	for (i = 0; i < event_count; i++) {

#if 1 /* SEB */
		if (!(events[i].events & EPOLLIN)) {
			continue;
		}
#endif

		/* The IN socket: listening, if there is an event, we should to open a new connection */
		if (core->sockets.in_listen == events[i].data.fd) {
			rc = pepa_in_accept_new_connection(core);

			/* If somethins happened during this process, we stop and return */
			if (PEPA_ERR_OK != rc) {
				return rc;
			}
			continue;
		}

		/* Else, it is a buffer waiting to be forwarded */

		fd_read = events[i].data.fd;

		/* Something on SHVA socket; transfer it to OUT */
		if (core->sockets.shva_rw == events[i].data.fd) {
			fd_write = core->sockets.out_write;
			fd_name_read = "SHVA";
			fd_name_write = "OUT";
			read_stat = &core->monitor.shva_rx;
			write_stat = &core->monitor.out_tx;
		} else { /* Else, it is an IN socket, transfer it to SHVA */
			fd_write = core->sockets.shva_rw;
			fd_name_read = "IN";
			fd_name_write = "SHVA";
			read_stat = &core->monitor.in_rx;
			write_stat = &core->monitor.shva_tx;
		}

		/* Read /write from/to socket */

		/* We must lock SHVA socket since IN can run several instances of this thread */
		rc = pepa_one_direction_copy3(/* Send to : */fd_write, fd_name_write,
									  /* From: */ fd_read, fd_name_read,
									  core->buffer, core->internal_buf_size * 1024,
									  /*Debug is ON */ 1,
									  /* RX stat */read_stat,
									  /* TX stat */write_stat,
									  /* Max iterations */ 1);

		if (PEPA_ERR_OK == rc) {
			//slog_warn_l("%s: Sent from socket %d", "IN-FORWARD", events[i].data.fd);
			continue;
		}

		slog_note_l("An error on sending buffers: %s", pepa_error_code_to_str(rc));

		/* Something wrong with the socket, should be removed */

		/* Writing side is off, means: SHVA socket is invalid */
		/* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
		if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
			slog_note_l("Could not write to %s; setting system to FAIL", fd_name_write);
			return TE_RESTART;
		}

		if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
			slog_note_l("Could not write to %s; setting system to FAIL", fd_name_write);
			/* Here are two cases: the read can be IN or SHVA. IN case of SHVA we must restart all sockets */
			if (fd_read == core->sockets.shva_rw) {
				return TE_RESTART;
			}

			/* If it is not SHVA, it is one of IN sockets; just close and remove from the set */
			slog_note_l("Could not write to %s; setting system to FAIL", fd_name_write);
			int rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);

			if (rc_remove) {
				slog_warn_l("%s: Could not remove socket %d from epoll set", "IN-READ", events[i].data.fd);
			}

			pepa_in_reading_sockets_close_rm(core, events[i].data.fd);
		}

	}
	return PEPA_ERR_OK;
}

int pepa3_transfer_loop(pepa_core_t *core)
{
	int                rc;
	struct epoll_event events[EVENTS_NUM];
	/* Wait until something happened */
	do {
		int32_t event_count = epoll_wait(core->epoll_fd, events, EVENTS_NUM, EPOLL_TIMEOUT);

		/* Interrupted by a signal */
		if (event_count < 0 && EINTR == errno) {
			continue;
		}

		/* Process exceptions */
		rc = pepa_process_exceptions(core, events, event_count);

		/* If there is an error, we must restart all sockets */
		if (PEPA_ERR_OK != rc) {
			return PST_CLOSE_SOCKETS;
		}

		/* Process buffers */
		rc = pepa_process_fdx(core, events, event_count);

		/* If there is an error, we must restart all sockets */
		if (PEPA_ERR_OK != rc) {
			return PST_CLOSE_SOCKETS;
		}
	} while (1);

	return PST_CLOSE_SOCKETS;
}

static void pepa_out_thread_open_listening_socket(pepa_core_t *core)
{
	struct sockaddr_in s_addr;
	int32_t            waiting_time = 0;
	int32_t            timeout      = 5;

	if (core->sockets.out_listen >= 0) {
		slog_note_l("Trying to open a listening socket while it is already opened");
	}
	do {
		/* Just try to close it */
		core->sockets.out_listen = pepa_open_listening_socket(&s_addr,
															  core->out_thread.ip_string,
															  core->out_thread.port_int,
															  core->out_thread.clients,
															  __func__);
		if (core->sockets.out_listen < 0) {
			core->sockets.out_listen = -1;
			//slog_warn_l("%s: Can not open listening socket: %s", my_name, strerror(errno));
			waiting_time += timeout;
			usleep(1000000);
		}
	} while (core->sockets.out_listen < 0);
}

static int32_t pepa_out_wait_connection(pepa_core_t *core, int32_t fd_listen)
{
	struct sockaddr_in s_addr;
	socklen_t          addrlen  = sizeof(struct sockaddr);
	int32_t            fd_read  = -1;
	do {
		slog_info_l("Starting accept() waiting");
		fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC);
	} while (fd_read < 0);

	slog_info_l("ACCEPTED CONNECTION: fd = %d", fd_read);
	pepa_set_tcp_timeout(fd_read);
	pepa_set_tcp_send_size(core, fd_read);
	return fd_read;
}

static void pepa_out_thread_accept(pepa_core_t *core)
{
	int32_t fd_read = pepa_out_wait_connection(core, core->sockets.out_listen);
	core->sockets.out_write = fd_read;
}

static void pepa_shva_thread_open_connection(pepa_core_t *core)
{
	/* Open connnection to the SHVA server */
	do {
		core->sockets.shva_rw = pepa_open_connection_to_server(core->shva_thread.ip_string->data, core->shva_thread.port_int, __func__); 

		if (core->sockets.shva_rw < 0) {
			core->sockets.shva_rw = -1;
			usleep(100000);
			continue;
		}
	} while (core->sockets.shva_rw < 0);

	slog_note_l("Opened connection to SHVA");
}

static void pepa_in_thread_listen_socket(pepa_core_t *core)
{
	struct sockaddr_in s_addr;
	while (1) {
		/* Just try to close it */
		pepa_socket_close_in_listen(core);
		core->sockets.in_listen = pepa_open_listening_socket(&s_addr,
															 core->in_thread.ip_string,
															 core->in_thread.port_int,
															 core->in_thread.clients,
															 __func__);
		if (core->sockets.in_listen >= 0) {
			return;
		}
		usleep(1000);
	}
}


/*********************************/
/*** State Machine functions ***/
/*********************************/


int pepa3_start(pepa_core_t *core)
{
	core->buffer = calloc(core->internal_buf_size * 1024, 1);

	if (NULL == core->buffer) {
		slog_error_l("Can not allocate a transfering buffer, stopped");
		exit(1);
	}
	core->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

	if (core->epoll_fd < 0) {
		slog_error_l("Can not create eventfd file descriptor, stopped");
		free(core->buffer);
		exit(2);
	}

	/* TODO: Instead of 1024 make it configurable */
	pepa_in_reading_sockets_allocate(core, 1024);

	slog_note_l("Finished 'start' phase");
	return PST_WAIT_OUT;
}

int pepa3_close_sockets(pepa_core_t *core)
{
	int rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.in_listen, NULL);
	if (rc) {
		slog_warn_l("Could not remove socket IN Listen from epoll set: fd: %d, %s", core->sockets.in_listen, strerror(errno));
	}

	rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.out_listen, NULL);
	if (rc) {
		slog_warn_l("Could not remove socket OUT Listen from epoll set: fd: %d, %s", core->sockets.out_listen, strerror(errno));
	}

	rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);
	if (rc) {
		slog_warn_l("Could not remove socket OUT Write from epoll set: %fd: %d, %s", core->sockets.out_write, strerror(errno));
	}

	rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.shva_rw, NULL);
	if (rc) {
		slog_warn_l("Could not remove socket SHVA from epoll set: fd: %d, %s", core->sockets.shva_rw, strerror(errno));
	}

	pepa_in_reading_sockets_close_all(core);
	rc = pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN");
	if (rc) {
		slog_warn_l("Could not close socket SHVA: fd: %d", core->sockets.shva_rw);
	}
	core->sockets.in_listen = -1;

	pepa_reading_socket_close(core->sockets.shva_rw, "SHVA");
	core->sockets.shva_rw = -1;

	pepa_socket_close(core->sockets.out_write, "OUT WRITE");
	core->sockets.out_write = -1;

	rc = pepa_socket_shutdown_and_close(core->sockets.out_listen, "OUT LISTEN");
	if (rc) {
		slog_warn_l("Could not close socket OUT LISTEN: fd: %d", core->sockets.out_listen);
	}

	core->sockets.out_listen = -1;

	slog_note_l("Finished 'close sockets' phase");
	return PST_WAIT_OUT;
}

int pepa3_wait_out(pepa_core_t *core)
{
	/* Both these functions are blocking and when they returned, both OUT sockets are opened */
	pepa_out_thread_open_listening_socket(core);
	pepa_out_thread_accept(core);

	if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.out_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		slog_error_l("Can not add OUT Listen socket to epoll set: %s", strerror(errno));
		return PST_CLOSE_SOCKETS;
	}

	if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		slog_error_l("Can not add OUT Write socket to epoll set: %s", strerror(errno));
		return PST_CLOSE_SOCKETS;
	}
	slog_note_l("Finished 'wait out' phase");
	return PST_OPEN_SHVA;
}

int pepa3_open_shva(pepa_core_t *core)
{
	/* This is an blocking function, returns only when SHVA is opened */
	pepa_shva_thread_open_connection(core);
	if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.shva_rw, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		slog_error_l("Can not add SHVA socket to epoll set: %s", strerror(errno));
		return PST_CLOSE_SOCKETS;
	}

	slog_note_l("Finished 'open shva' phase");
	return PST_START_IN;
}

int pepa3_start_in(pepa_core_t *core)
{
	/* This is a blocking function */
	pepa_in_thread_listen_socket(core);
	if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.in_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		slog_error_l("Can not add IN Listen socket to epoll set: %s", strerror(errno));
		return PST_CLOSE_SOCKETS;
	}
	slog_note_l("Finished 'start in' phase");
	return PST_TRANSFER_LOOP;
}

int pepa3_end(pepa_core_t *core)
{
	pepa3_close_sockets(core);
	pepa_in_reading_sockets_free(core);
	slog_note_l("Finished 'end' phase");
	return PEPA_ERR_OK;
}

int pepa_go(pepa_core_t *core)
{
	//int rc;
	int next_state = PST_START;

	do {
		switch (next_state) {
		case PST_START:
			next_state = pepa3_start(core);
			break;
		case PST_CLOSE_SOCKETS:
			next_state = pepa3_close_sockets(core);
			break;
		case PST_WAIT_OUT:
			next_state = pepa3_wait_out(core);
			break;
		case PST_OPEN_SHVA:
			next_state = pepa3_open_shva(core);
			break;
		case PST_START_IN:
			next_state = pepa3_start_in(core);
			break;
		case PST_TRANSFER_LOOP:
			next_state = pepa3_transfer_loop(core);
			break;
		case PST_END:
			return pepa3_end(core);
		}
	} while (1);

	return PEPA_ERR_OK;
}

