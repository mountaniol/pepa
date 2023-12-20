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
#include "pepa_socket_shva.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_debug.h"
#include "pepa_state_machine.h"
#include "buf_t/buf_t.h"
#include "buf_t/se_debug.h"

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Open connection to shva, return file descriptor
 * @return int file descriptor of socket to shva, >= 0;
 *  	   a negative error code on an error
 */
static int pepa_open_shava_connection(void)
{
	pepa_core_t *core                      = pepa_get_core();
	return pepa_open_connection_to_server(core->shva_thread.ip_string->data, core->shva_thread.port_int, __func__);
}

static int pepa_shva_thread_start_transfer(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	pthread_t  pthread_id;
	pepa_fds_t *fds       = pepa_fds_t_alloc(core->sockets.shva_rw, /* Read from this socket */
												   core->sockets.out_write, /* Write into this socket*/
												   -1, /* Send me signal when you die using this file descriptor */
											 //core->controls.shva_from_out_die, /* Don't listen any 'die' event */
												   -1, /* Don't listen any 'die' event */
												   NULL, /* Do not use any semaphor */
												   "SHVA", "SHVA", "OUT" /*Starter thread name */);

	/* Start the new thread copying between this new read socket and writing to shva socket */
	int        thread_rc  = pthread_create(&pthread_id, NULL, pepa_one_direction_rw_thread, fds);
	if (thread_rc < 0) {
		DE("SHVA: Could not start new thread\n");
		return -1;
	}

	DD("%s: A new forwarding thread is up\n", my_name);
	return PEPA_ERR_OK;
}

static int pepa_shva_thread_open_connection(pepa_core_t *core, const char *my_name)
{
	/* Open connnection to the SHVA server */
	do {
		core->sockets.shva_rw = pepa_open_shava_connection();

		if (core->sockets.shva_rw < 0) {
			DE("%s: Can not open connection to SHVA server; wait 10 seconds and try over\n", my_name);
			sleep(10);
			continue;
		}
	} while (core->sockets.shva_rw < 0);

	DD("%s: Opened connection to SHVA\n", my_name);
	return PEPA_ERR_OK;
}

static int pepa_shva_thread_close_socket(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	close(core->sockets.shva_rw);
	core->sockets.shva_rw = -1;

	close(core->sockets.in_listen);
	core->sockets.in_listen = -1;

	close(core->sockets.out_listen);
	core->sockets.out_listen = -1;

	close(core->sockets.out_write);
	core->sockets.out_write = -1;

	DD("%s: Closed connection to SHVA\n", my_name);
	return 0;
}

static int pepa_shva_thread_watch(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	if (0 != pepa_test_fd(core->sockets.shva_rw)) {
		return -1;
	}
	return 0;
}


void *pepa_shva_thread_new(__attribute__((unused))void *arg)
{
	int                      rc;
	const char               *my_name  = "SHVA";
	pepa_core_t              *core     = pepa_get_core();
	int                      read_sock = -1;
	pepa_shva_thread_state_t next_step = PEPA_TH_SHVA_START;

	do {
		pepa_shva_thread_state_t this_step = next_step;
		switch (next_step) {
		case 	PEPA_TH_SHVA_START:
			DD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			rc = pepa_pthread_init_phase(my_name);
			if (rc < 0) {
				DE("%s: Could not init the thread\n", my_name);
				pepa_state_set(core, PEPA_PR_IN, PEPA_ST_FAIL, __func__, __LINE__);
				next_step = PEPA_TH_SHVA_TERMINATE;
			}
			DD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_OPEN_CONNECTION:
			DD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_START_TRANSFER;
			rc = pepa_shva_thread_open_connection(core, my_name);
			if (PEPA_ERR_OK != rc) {
				next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			}
			DD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_TEST_CONNECTION:
			DD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_START_TRANSFER;
			if (pepa_test_fd(core->sockets.shva_rw) < 0) {
				next_step = PEPA_TH_SHVA_CLOSE_SOCKET;
			}
			DD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_START_TRANSFER:
			DD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			/* TODO: Different steps depends on return status */
			read_sock = pepa_shva_thread_start_transfer(core, my_name);
			if (read_sock < 0) {
				DD("Could not open listen socket\n");
				next_step = PEPA_TH_SHVA_CLOSE_SOCKET;
			}
			next_step = PEPA_TH_SHVA_WATCH_SOCKET;
			pepa_state_set(core, PEPA_PR_SHVA, PEPA_ST_RUN, __func__, __LINE__);
			DD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_WATCH_SOCKET:
			//DD("START STEP: %s\n", pepa_shva_thread_state_str(next_step));
			next_step = PEPA_TH_SHVA_WATCH_SOCKET;
			rc = pepa_shva_thread_watch(core, my_name);
			if (rc) {
				next_step = PEPA_TH_SHVA_CLOSE_SOCKET;
			}
			//DD("END STEP:   %s\n", pepa_shva_thread_state_str(next_step));
			usleep(1000);
			break;

		case PEPA_TH_SHVA_CLOSE_SOCKET:
			DD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			rc = pepa_shva_thread_close_socket(core, my_name);
			next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			DD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_TERMINATE:
			DD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			pepa_state_set(core, PEPA_PR_IN, PEPA_ST_FAIL, __func__, __LINE__);
			sleep(10);
			DD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
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

	return NULL;

	/* Tell to CTL that we are in air */
	DD("SHVA: Sending CONNECTED signal to CLT\n");
	//pepa_event_send(core->controls.ctl_from_shva_started, 1);
	pepa_state_set(core, PEPA_PR_SHVA, PEPA_ST_RUN, __func__, __LINE__);

	/* Wait for event from subthread (pepa_one_direction_rw_thread) and terminate id it terminated */
	do {
		usleep(50);
		if (0 != pepa_test_fd(core->sockets.shva_rw)) {
			DE("SHVA: core->sockets.shva_rw is invalid: %d, terminate\n", core->sockets.shva_rw);
			pepa_state_set(core, PEPA_PR_SHVA, PEPA_ST_FAIL, __func__, __LINE__);
			sleep(10);
		}
	} while (1);

	return NULL;
}


