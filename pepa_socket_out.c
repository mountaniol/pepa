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
		DD("1\n");
		//int       error_action;
		DD("%s: Starting accept() waiting\n", my_name);

		fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC);
//		       int accept4(int sockfd, struct sockaddr *addr,
//                   socklen_t *addrlen, int flags);

	} while (fd_read < 0);

	return fd_read;
}

static int pepa_out_thread_start(char *name)
{
	return pepa_pthread_init_phase(name);
}

static int pepa_out_thread_socket(pepa_core_t *core, char *my_name)
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
			DDD("OUT: Can not open listening socket: %s\n", strerror(errno));
			usleep(timeout * 1000000);
			waiting_time += timeout;
			DD("%s: Wait for %d secs\n", my_name, waiting_time);
		}
	} while (core->sockets.out_listen < 0);
	return PEPA_ERR_OK;
}

static int pepa_out_thread_accept(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	int fd_read = pepa_out_wait_connection(core->sockets.out_listen);
	pepa_core_lock();
	core->sockets.out_write = fd_read; 
	pepa_core_unlock();
	return PEPA_ERR_OK;
}

static int pepa_out_thread_forward(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	do {
		usleep(50);
		if (0 != pepa_test_fd(core->sockets.out_write)) {
			DE("OUT: core->sockets.out_read is invalid: %d, terminate\n", core->sockets.out_write);
			return -1;
		}
	} while (1);
	return 0;
}

static int pepa_out_thread_close_listen(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	close(core->sockets.out_write);
	core->sockets.out_write = -1;
	return 0;
}

static int pepa_out_thread_close_socket(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	close(core->sockets.shva_rw);
	core->sockets.shva_rw = -1;

	close(core->sockets.in_listen);
	core->sockets.in_listen = -1;

	close(core->sockets.out_listen);
	core->sockets.out_listen = -1;

	close(core->sockets.out_write);
	core->sockets.out_write = -1;

	return 0;
}

static int pepa_out_thread_watch_write_socket(pepa_core_t *core, __attribute__((unused))char *my_name)
{
	do {
		if (pepa_test_fd(core->sockets.out_write) <  0) {
			return 0;
		}
	} while (1);
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
			DD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_CREATE_LISTEN;
			rc = pepa_out_thread_start(my_name);
			if (rc < 0) {
				DE("%s: Could not init the thread\n", my_name);
				pepa_state_set(core, PEPA_PR_OUT, PEPA_ST_FAIL, __func__, __LINE__);
				next_step = PEPA_TH_OUT_TERMINATE;
			}
			DD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CREATE_LISTEN:
			DD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_ACCEPT;
			rc = pepa_out_thread_socket(core, my_name);
			if (rc) {
				next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
			}
			DD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_ACCEPT:
			DD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_START_TRANSFER;
			if (0 != pepa_test_fd(core->sockets.out_listen)) {
				next_step	= PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
				break;
			}

			rc = pepa_out_thread_accept(core, my_name);
			if (rc) {
				next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
			}
			DD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_START_TRANSFER:
			DD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			pepa_state_set(core, PEPA_PR_OUT, PEPA_ST_RUN, __func__, __LINE__);
			rc = pepa_out_thread_forward(core, my_name);
			next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
			DD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_WATCH_WRITE_SOCK:
			DD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			/* TODO */
			rc = pepa_out_thread_watch_write_socket(core, my_name);
			next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
			DD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CLOSE_LISTEN_SOCKET:
			DD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			rc = pepa_out_thread_close_listen(core, my_name);
			next_step = PEPA_TH_OUT_ACCEPT;
			DD("END STEP\n");
			DD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_CLOSE_WRITE_SOCKET:
			DD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			rc = pepa_out_thread_close_socket(core, my_name);
			next_step = PEPA_TH_OUT_CREATE_LISTEN;
			DD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		case PEPA_TH_OUT_TERMINATE:
			DD("START STEP: %s\n", pepa_out_thread_state_str(this_step));
			pepa_state_set(core, PEPA_PR_OUT, PEPA_ST_FAIL, __func__, __LINE__);
			sleep(10);
			DD("END STEP  : %s\n", pepa_out_thread_state_str(this_step));
			break;

		default:
			DD("Should never be here: next_steps = %d\n", next_step);
			abort();
			break;
		}
	} while (1);
	DD("Should never be here\n");
	abort();
	pthread_exit(NULL);
}

