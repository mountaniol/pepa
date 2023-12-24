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

#if 0 /* SEB */
static int pepa_shva_thread_start_transfer(pepa_core_t *core, __attribute__((unused)) const char *my_name){
	pthread_t pthread_id;
	int       rc         = pepa_test_fd(core->sockets.shva_rw);
	if (rc < 0) {
		DE("%s: SHVA connection is invalid and should be re-opened\n", my_name);
		return -PEPA_ERR_BAD_SOCKET_READ;
	}

	pepa_fds_t *fds      = pepa_fds_t_alloc(core->sockets.shva_rw, /* Read from this socket */
											core->sockets.out_write, /* Write into this socket*/
											1, /* Do not close the reading socket on exit from thread */
											NULL, /* Do not use any semaphor */
											"SHVA", "SHVA", "OUT" /*Starter thread name */);

	/* Start the new thread copying between this new read socket and writing to shva socket */
	int        thread_rc = pthread_create(&pthread_id, NULL, pepa_one_direction_rw_thread, fds);
	if (thread_rc < 0) {
		DDE("SHVA: Could not start new thread\n");
		return -PEPA_ERR_THREAD_CANNOT_CREATE;
	}

	core->shva_transfet_thread.thread_id = pthread_id;

	DDD("%s: A new forwarding thread is up\n", my_name);
	return PEPA_ERR_OK;
}
#endif

static int pepa_shva_thread_open_connection(pepa_core_t *core, const char *my_name)
{
	/* Open connnection to the SHVA server */
	do {
		core->sockets.shva_rw = pepa_open_shava_connection();

		if (core->sockets.shva_rw < 0) {
			DDE("%s: Can not open connection to SHVA server; wait 1 seconds and try over\n", my_name);
			usleep(1 * 1000000);
			continue;
		}
	} while (core->sockets.shva_rw < 0);

	DDD("%s: Opened connection to SHVA\n", my_name);
	return PEPA_ERR_OK;
}

static int pepa_shva_thread_close_socket(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	//int rc = pepa_close_socket(core->sockets.shva_rw, my_name);
	int sock = core->sockets.shva_rw;
	int rc   = close(sock);
	if (rc < 0) {
		DDDE0("%s: Could not close the socket: fd: %d, %s\n", my_name, sock, strerror(errno));
		return -1;
	}

	core->sockets.shva_rw = -1;
	return 0;
}

/* TODO: This should be based on a signal from SHVA */
static int pepa_shva_thread_wait_out(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	while (1) {
		if (PEPA_ST_RUN == pepa_state_out_get(core)) {
			DDD("%s: OUT is UP\n", my_name);
			return 0;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		DDD("SHVA GOT SIGNAL\n");
	};
	return 0;
}

#if 0 /* SEB */
static int pepa_shva_thread_watch(pepa_core_t *core, __attribute__((unused)) const char *my_name){
	do {
		if (0 != pepa_test_fd(core->sockets.shva_rw)) {
			DDD("SHVA socket is invalid\n");
			return -1;
		}

		/* If the transfer thread is dead, we should restart it */
		if (pthread_kill(core->shva_transfet_thread.thread_id, 0) < 0) {
			core->shva_transfet_thread.thread_id = PTHREAD_DEAD;
			DDD("SHVA transfering thread is terminated\n");
			return 1;
		}
		usleep(1000);

	} while (1);
	return 0;
}
#endif

/* Wait for signal; when SHVA is DOWN, return */
static int pepa_shva_thread_wait_fail(pepa_core_t *core, __attribute__((unused)) const char *my_name)
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

void *pepa_shva_thread_new_forward(__attribute__((unused))void *arg);
void *pepa_shva_thread_new(__attribute__((unused))void *arg)
{
	pthread_t                shva_forwarder;
	int                      rc;
	const char               *my_name       = "SHVA";
	pepa_core_t              *core          = pepa_get_core();
	// int                      read_sock = -1;
	pepa_shva_thread_state_t next_step      = PEPA_TH_SHVA_START;

	do {
		pepa_shva_thread_state_t this_step = next_step;
		switch (next_step) {
		case 	PEPA_TH_SHVA_START:
			DDD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			rc = pepa_pthread_init_phase(my_name);
			if (rc < 0) {
				DE("%s: Could not init the thread\n", my_name);
				pepa_state_shva_set(core, PEPA_ST_FAIL);
				next_step = PEPA_TH_SHVA_TERMINATE;
			}
			DDD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_OPEN_CONNECTION:
			DDD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_WAIT_OUT;
			rc = pepa_shva_thread_open_connection(core, my_name);
			if (PEPA_ERR_OK != rc) {
				next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			} else {
				/* By this signal IN thread should start transfer data */
				pepa_state_shva_set(core, PEPA_ST_RUN);
				DDD("%s: SHVA set itself UP\n", my_name);
			}
			DDD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_WAIT_OUT:
			DDD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_START_TRANSFER;
			rc = pepa_shva_thread_wait_out(core, my_name);
			if (PEPA_ERR_OK != rc) {
				next_step = PEPA_TH_SHVA_WAIT_OUT;
			}
			DDD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_START_TRANSFER:
			DDD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			/* TODO: Different steps depends on return status */
			// rc = pepa_shva_thread_start_transfer(core, my_name);
			rc = pthread_create(&shva_forwarder, NULL, pepa_shva_thread_new_forward, NULL);
			if (rc < 0) {
				DDDE("%s: Could not start listening socket\n", my_name);
				next_step = PEPA_TH_SHVA_CLOSE_SOCKET;
			}
			next_step = PEPA_TH_SHVA_WATCH_SOCKET;
			DDD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_WATCH_SOCKET:
			DDD("START STEP: %s\n", pepa_shva_thread_state_str(next_step));
			next_step = PEPA_TH_SHVA_WATCH_SOCKET;
			/// rc = pepa_shva_thread_watch(core, my_name);
			rc = pepa_shva_thread_wait_fail(core, my_name);
			/* SHVA socket is invalid, we must reconnect */

			if (PEPA_ERR_OK != pepa_test_fd(core->sockets.shva_rw)) {
				next_step = PEPA_TH_SHVA_CLOSE_SOCKET;
			} else {
				next_step = PEPA_TH_SHVA_WAIT_OUT;
			}

			DDD("END STEP:   %s\n", pepa_shva_thread_state_str(next_step));
			//usleep(1000);
			break;

		case PEPA_TH_SHVA_CLOSE_SOCKET:
			DDD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			pepa_state_shva_set(core, PEPA_ST_FAIL);
			rc = pepa_shva_thread_close_socket(core, my_name);
			next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			DDD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_TERMINATE:
			DDD("START STEP: %s\n", pepa_shva_thread_state_str(this_step));
			pepa_state_shva_set(core, PEPA_ST_FAIL);
			sleep(10);
			DDD("END STEP:   %s\n", pepa_shva_thread_state_str(this_step));
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

#define BUF_SIZE (2048)
void *pepa_shva_thread_new_forward(__attribute__((unused))void *arg)
{
	int         rc;
	pepa_core_t *core            = pepa_get_core();
	char        *my_name         = "SHVA-FORWARD";

	int         activity;
	int         max_sd           = 0;

	char        buffer[BUF_SIZE];  //data buffer of 1K

	//set of socket descriptors
	fd_set      readfds;

	rc = pepa_pthread_init_phase(my_name);
	if (rc < 0) {
		DE("%s: Could not init the thread\n", my_name);
		core->sockets.shva_rw = -1;
		pepa_state_shva_set(core, PEPA_ST_FAIL);
		pthread_exit(NULL);
	}

	//accept the incoming connection

	int sock_in  = core->sockets.shva_rw;
	int sock_out = core->sockets.out_write;

	DDD("%s: socket in (SHVA): %d, socket out (OUT): %d\n", my_name, sock_in, sock_out);

	while (1)   {
		//clear the socket set
		FD_ZERO(&readfds);

		/* Wait until daya on SHVA socket */
		FD_SET(sock_in, &readfds);
		max_sd = sock_in;

		activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

		/* Interrupted by a signal, continue */
		if ((activity < 0) && (errno == EINTR)) {
			DDD0("%s: Select interrupted by a signal\n", my_name);
			continue;
		}

		if ((activity < 0) && (errno != EINTR)) {
			DE("%s: select error: %s\n", my_name, strerror(errno));
			close(core->sockets.shva_rw);
			core->sockets.shva_rw = -1;
			pepa_state_shva_set(core, PEPA_ST_FAIL);
			pthread_exit(NULL);
		}

		if (FD_ISSET(sock_in, &readfds)) {
			DDD0("%s: Socket has something on it\n", my_name);
			rc = pepa_one_direction_copy2(/* Send to : */sock_out, "OUT", 
										  /* From: */sock_in, "SHVA",
										  buffer, BUF_SIZE, /* Debug off */ 0);
			DDD0("%s: Finished copying to OUT\n", my_name);
			if (PEPA_ERR_OK == rc) {
				DDD0("%s: Sent to OUT OK\n", my_name);
				continue;
			}

			if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
				DDD("%s: Could not write to OUT, closing read IN socket\n", my_name);
				close(core->sockets.shva_rw);
				core->sockets.shva_rw = -1;
				pepa_state_shva_set(core, PEPA_ST_FAIL);
				pthread_exit(NULL);
			}

			if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
				DDD("%s: Could not write to OUT, closing read IN socket\n", my_name);
				close(core->sockets.shva_rw);
				core->sockets.shva_rw = -1;
				pepa_state_out_set(core, PEPA_ST_FAIL);
				pthread_exit(NULL);
			}

		}
	}

	pthread_exit(NULL);
}

