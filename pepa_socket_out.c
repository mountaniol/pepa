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
		//int       error_action;
		DDD("%s: Starting accept() waiting\n", my_name);

		fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC);
//		       int accept4(int sockfd, struct sockaddr *addr,
//                   socklen_t *addrlen, int flags);

	} while (fd_read < 0);

	DDD("%s: ACCEPTED CONNECTION: fd = %d\n", my_name, fd_read);
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
			DDE("%s: Can not open listening socket: %s\n", my_name, strerror(errno));
			//usleep(timeout * 1000000);
			waiting_time += timeout;
			///DDD("%s: Wait for %d secs\n", my_name, waiting_time);
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

static int pepa_out_thread_close_listen(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	int rc = pepa_socket_shutdown_and_close(core->sockets.out_listen, my_name);
	if (rc < 0) {
		DE("%s: Could not shutdown the socket: fd: %d, %s\n", my_name, core->sockets.out_listen, strerror(errno));
		return -1;
	}

	core->sockets.out_listen = -1;
	return 0;
}

static int pepa_out_thread_close_write_socket(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	int sock = core->sockets.out_write;
	int rc   = close(sock);
	if (rc < 0) {
		DE("%s: Could not close the socket: fd: %d, %s\n", my_name, sock, strerror(errno));
		return -1;
	}

	core->sockets.out_write = -1;

	DDD("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
	DDD("$$$$$$$    CLOSING OUT WRITE SOCK        $$$$$$$$$\n");
	DDD("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
	return rc;
}

#if 0 /* SEB */
static int pepa_out_thread_watch_write_socket(pepa_core_t *core, __attribute__((unused))char *my_name){
	do {
		if (PEPA_ERR_OK != pepa_test_fd(core->sockets.out_write)) {
			return -1;
		}
		usleep(core->monitor_timeout);
	} while (1);
	return 0;
}
#endif

/* Wait for signal; when SHVA is DOWN, return */
static int pepa_out_thread_wait_fail(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	while (1) {
		if (PEPA_ST_FAIL == pepa_state_out_get(core)) {
			DDD("%s: OUT became DOWN\n", my_name);
			return 0;
		}

		if (PEPA_ST_FAIL == pepa_state_shva_get(core)) {
			DDD("%s: SHVA became DOWN\n", my_name);
			return 0;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		DDD("SHVA GOT SIGNAL\n");
	};
	return 0;
}


void *pepa_out_thread(__attribute__((unused))void *arg)
{
	pepa_out_thread_state_t next_step = PEPA_TH_OUT_START;
	char                    *my_name  = "OUT";
	int                     rc;
	pepa_core_t             *core     = pepa_get_core();

	do {
		pepa_out_thread_state_t this_step = next_step;
		switch (next_step) {
		case 	PEPA_TH_OUT_START:
			DDD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_CREATE_LISTEN;
			rc = pepa_out_thread_start(my_name);
			if (rc < 0) {
				DE("%s: Could not init the thread\n", my_name);
				pepa_state_out_set(core, PEPA_ST_FAIL);
				next_step = PEPA_TH_OUT_TERMINATE;
			}
			DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CREATE_LISTEN:
			DDD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_ACCEPT;
			rc = pepa_out_thread_open_listening_socket(core, my_name);
			if (rc) {
				DDE("%s: Can not open listening socket\n", my_name);
				next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
			}
			DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_ACCEPT:
			DDD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_WATCH_WRITE_SOCK;
			if (0 != pepa_test_fd(core->sockets.out_listen)) {
				DDE("%s: Can not start accept: listening socket is invalid: fd %d\n", my_name, core->sockets.out_listen);
				next_step	= PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
				DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
				break;
			}

			rc = pepa_out_thread_accept(core, my_name);
			if (rc) {
				DDE("%s: Can not accept incoming connection\n", my_name);
				next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
				DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
				break;
			}

			pepa_state_out_set(core, PEPA_ST_RUN);

			DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_WATCH_WRITE_SOCK:
			DDD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			/* TODO */
			//rc = pepa_out_thread_watch_write_socket(core, my_name);
			rc = pepa_out_thread_wait_fail(core, my_name);
			next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
			DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CLOSE_LISTEN_SOCKET:
			DDD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			rc = pepa_out_thread_close_listen(core, my_name);
			next_step = PEPA_TH_OUT_ACCEPT;
			DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CLOSE_WRITE_SOCKET:
			DDD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			rc = pepa_out_thread_close_write_socket(core, my_name);
			next_step = PEPA_TH_OUT_CREATE_LISTEN;
			DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_TERMINATE:
			DDD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			pepa_state_out_set(core, PEPA_ST_FAIL);
			sleep(10);
			DDD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		default:
			DE("Should never be here: next_steps = %d\n", next_step);
			abort();
			break;
		}
	} while (1);
	DE("Should never be here\n");
	abort();
	pthread_exit(NULL);
}

