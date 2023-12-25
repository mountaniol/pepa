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
#include <sys/epoll.h>

#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_socket_out.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_debug.h"
#include "pepa_state_machine.h"
#include "buf_t/buf_t.h"
#include "buf_t/se_debug.h"

static int pepa_out_wait_connection(int fd_listen)
{
	struct sockaddr_in s_addr;
	socklen_t          addrlen  = sizeof(struct sockaddr);
	const char         *my_name = "OUT-ACCEPT";
	int                fd_read  = -1;
	do {
		slog_note("%s: Starting accept() waiting", my_name);

		fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC);

	} while (fd_read < 0);

	slog_note("%s: ACCEPTED CONNECTION: fd = %d", my_name, fd_read);
	return fd_read;
}

static int pepa_out_thread_start(char *name)
{
	return pepa_pthread_init_phase(name);
}

static int pepa_out_thread_open_listening_socket(pepa_core_t *core, char *my_name)
{
	struct sockaddr_in s_addr;
	int                waiting_time = 0;
	int                timeout      = 5;
	do {
		/* Just try to close it */
		core->sockets.out_listen = pepa_open_listening_socket(&s_addr,
															  core->out_thread.ip_string,
															  core->out_thread.port_int,
															  core->out_thread.clients,
															  __func__);
		if (core->sockets.out_listen < 0) {
			core->sockets.out_listen = -1;
			slog_warn("%s: Can not open listening socket: %s", my_name, strerror(errno));
			waiting_time += timeout;
		}
	} while (core->sockets.out_listen < 0);
	return PEPA_ERR_OK;
}

static int pepa_out_thread_accept(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	int fd_read = pepa_out_wait_connection(core->sockets.out_listen);
	core->sockets.out_write = fd_read;
	return PEPA_ERR_OK;
}
static int pepa_out_thread_close_write_socket(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	int sock = core->sockets.out_write;
	int rc   = close(sock);
	if (rc < 0) {
		slog_error("%s: Could not close the socket: fd: %d, %s", my_name, sock, strerror(errno));
		return -PEPA_ERR_CANNOT_CLOSE;
	}

	core->sockets.out_write = -1;

	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_note("$$$$$$$    CLOSED <OUT> WRITE SOCK       $$$$$$$$$");
	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	return rc;
}

static int pepa_out_thread_close_listen(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	int rc = pepa_out_thread_close_write_socket(core,my_name);
	if (PEPA_ERR_OK != rc) {
		slog_error("%s: Could not closw write socket", my_name);
	}

	rc = pepa_socket_shutdown_and_close(core->sockets.out_listen, my_name);
	if (rc < 0) {
		slog_error("%s: Could not shutdown the socket: fd: %d, %s", my_name, core->sockets.out_listen, strerror(errno));
		return -PEPA_ERR_CANNOT_SHUTDOWN;
	}

	core->sockets.out_listen = -1;

	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_note("$$$$$$$    CLOSED <OUT> LISTEN SOCK      $$$$$$$$$");
	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	return PEPA_ERR_OK;
}

/* Wait for signal; when SHVA is DOWN, return */
static int pepa_out_thread_wait_fail(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	while (1) {
		if (PEPA_ST_FAIL == pepa_state_out_get(core)) {
			slog_note("%s: OUT became DOWN", my_name);
			return PEPA_ERR_OK;
		}

		if (PEPA_ST_FAIL == pepa_state_shva_get(core)) {
			slog_note("%s: SHVA became DOWN", my_name);
			return PEPA_ERR_OK;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note("GOT SOME SIGNAL");
	};
	return PEPA_ERR_OK;
}

void *pepa_out_thread(__attribute__((unused))void *arg)
{
	pepa_out_thread_state_t next_step = PEPA_TH_OUT_START;
	char                    *my_name  = "OUT";
	int                     rc;
	pepa_core_t             *core     = pepa_get_core();

	set_sig_handler();

	do {
		pepa_out_thread_state_t this_step = next_step;
		switch (next_step) {
		case 	PEPA_TH_OUT_START:
			slog_note("START STEP: %s", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_CREATE_LISTEN;
			rc = pepa_out_thread_start(my_name);
			if (rc < 0) {
				slog_fatal("%s: Could not init the thread", my_name);
				pepa_state_out_set(core, PEPA_ST_FAIL);
				next_step = PEPA_TH_OUT_TERMINATE;
			}
			slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CREATE_LISTEN:
			slog_note("START STEP: %s", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_ACCEPT;
			rc = pepa_out_thread_open_listening_socket(core, my_name);
			if (rc) {
				slog_warn("%s: Can not open listening socket", my_name);
				next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
			}
			slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_ACCEPT:
			slog_note("START STEP: %s", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_WATCH_WRITE_SOCK;
			if (0 != pepa_test_fd(core->sockets.out_listen)) {
				slog_warn("%s: Can not start accept: listening socket is invalid: fd %d", my_name, core->sockets.out_listen);
				next_step	= PEPA_TH_OUT_CLOSE_LISTEN_SOCKET;
				slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
				break;
			}

			rc = pepa_out_thread_accept(core, my_name);
			if (rc) {
				slog_warn("%s: Can not accept incoming connection", my_name);
				next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
				slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
				break;
			}

			pepa_state_out_set(core, PEPA_ST_RUN);

			slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_WATCH_WRITE_SOCK:
			slog_note("START STEP: %s", pepa_out_thread_state_str(this_step));
			/* TODO */
			//rc = pepa_out_thread_watch_write_socket(core, my_name);
			rc = pepa_out_thread_wait_fail(core, my_name);
			next_step = PEPA_TH_OUT_CLOSE_LISTEN_SOCKET;
			slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CLOSE_LISTEN_SOCKET:
			slog_note("START STEP: %s", pepa_out_thread_state_str(this_step));
			//rc = pepa_out_thread_close_write_socket(core, my_name);
			rc = pepa_out_thread_close_listen(core, my_name);
			next_step = PEPA_TH_OUT_CREATE_LISTEN;
			slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CLOSE_WRITE_SOCKET:
			slog_note("START STEP: %s", pepa_out_thread_state_str(this_step));
			rc = pepa_out_thread_close_write_socket(core, my_name);
			next_step = PEPA_TH_OUT_ACCEPT;
			slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_TERMINATE:
			slog_note("START STEP: %s", pepa_out_thread_state_str(this_step));
			pepa_state_out_set(core, PEPA_ST_FAIL);
			sleep(10);
			slog_note("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

		default:
			slog_fatal("Should never be here: next_steps = %d", next_step);
			abort();
			break;
		}
	} while (1);
	slog_fatal("Should never be here");
	abort();
	pthread_exit(NULL);
}

