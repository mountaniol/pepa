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
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_debug.h"
#include "pepa_state_machine.h"
#include "buf_t/buf_t.h"
#include "buf_t/se_debug.h"

static int pepa_in_thread_wait_shva(pepa_core_t *core, __attribute__((unused)) const char *my_name);

static int pepa_in_thread_subthread(pepa_core_t *core, __attribute__((unused)) const char *my_name, const int read_sock)
{
	pthread_t pthread_id;
	int       iteration  = 0;
	int       shva_sock;


	PEPA_SOCK_LOCK(core->sockets.shva_rw, core);
	shva_sock = core->sockets.shva_rw;

	/* Just in case test status of SHVA */
	if (shva_sock < 0) {
		DDDE("IN: SHVA socket is dead\n");
		PEPA_SOCK_UNLOCK(core->sockets.shva_rw, core);
		int rc = pepa_in_thread_wait_shva(core, my_name);
		if (rc < 0) {
			DDDE("IN: SHVA socket is dead, stop\n");
			return -1;
		}
		//return -1;
	}

	DDD("IN SUBTHREAD: Starting with read fd = %d, write fd = %d\n", read_sock, shva_sock);

	pepa_fds_t *fds      = pepa_fds_t_alloc(read_sock, /* Read from this socket */
												 shva_sock, /* Write to this socket*/
												 -1, /* Do not send me signal when you die */
												 -1, /* Listen this event fd and die when there is an event */
												 &core->sockets.shva_rw_mutex /* Use this mutex for write operation sync */,
												 "IN", "IN", "SHVA" /* Starter thread name */);

	/* Start the new thread copying between this new read socket and writing to shva socket */
	int        thread_rc = pthread_create(&pthread_id, NULL, pepa_one_direction_rw_thread, fds);
	PEPA_SOCK_UNLOCK(core->sockets.shva_rw, core);

	if (thread_rc < 0) {
		DE("IN: Could not start new thread, iter: %d\n", iteration);
		return -1;
	}
	return PEPA_ERR_OK;
}

static int pepa_in_thread_listen(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	int                iteration = 0;
	struct sockaddr_in s_addr;
	socklen_t          addrlen   = sizeof(struct sockaddr);

	do {
		iteration++;

		DDD("IN ACCEPT: Starting 'accept' waiting, iter: %d\n", iteration);
		PEPA_SOCK_LOCK(core->sockets.in_listen, core);

		if (PEPA_ERR_OK != pepa_test_fd(core->sockets.in_listen)) {
			PEPA_SOCK_UNLOCK(core->sockets.in_listen, core);
			return -PEPA_ERR_SOCKET_LISTEN;
		}

		int read_sock = accept(core->sockets.in_listen, &s_addr, &addrlen);

		PEPA_SOCK_UNLOCK(core->sockets.in_listen, core);

		if (read_sock >= 0) {
			DDD("IN ACCEPT: Accepted IN connection, iter: %d, fd: %d\n", iteration, read_sock);
			return read_sock;
		}

		/* If something went wrong, analyze the error and decide what to do */
		if (read_sock < 0) {
			int err = errno;
			DE("IN ACCEPT: failed, listening socket: %d: %s | %d\n", core->sockets.in_listen, strerror(err), EINVAL);

			if (EINTR != err) {
				return -PEPA_ERR_SOCKET_LISTEN;
			}

		}

		//usleep(1 * 1000000);

		DDD("ACCEPT: Continue acept() nex iter\n");
	} while (1);

	return -1;
}

static void pepa_in_thread_close_listen(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	int rc = pepa_socket_shutdown_and_close(core->sockets.in_listen, my_name);
	if (rc < 0) {
		DE("%s: Could not shutdownclose the listening socket: fd: %d, %s\n", my_name, core->sockets.in_listen, strerror(errno));
		return;
	}
	core->sockets.in_listen = -1;
}

static void pepa_in_thread_listen_socket(pepa_core_t *core, const char *my_name)
{
	struct sockaddr_in s_addr;

	while (1) {
		DDD("%s: Opening listening socket\n", my_name);
		/* Just try to close it */
		PEPA_SOCK_LOCK(core->sockets.in_listen, core);
		pepa_in_thread_close_listen(core, __func__);

		core->sockets.in_listen = pepa_open_listening_socket(&s_addr,
															 core->in_thread.ip_string,
															 core->in_thread.port_int,
															 core->in_thread.clients,
															 __func__);
		PEPA_SOCK_UNLOCK(core->sockets.in_listen, core);

		if (core->sockets.in_listen >= 0) {
			return;
		}
		sleep(3);
	}
}

/* TODO: This should be based on a signal from SHVA */
static int pepa_in_thread_wait_shva(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	int sock = 0;
	while (sock >= 0) {
		PEPA_SOCK_LOCK(core->sockets.shva_rw, core);
		int sock = core->sockets.shva_rw;

		if (PEPA_ERR_OK == pepa_test_fd(sock)) {
			PEPA_SOCK_UNLOCK(core->sockets.shva_rw, core);
			return 0;
		}
		PEPA_SOCK_UNLOCK(core->sockets.shva_rw, core);
		usleep(1000);
	} while (1);
	return 0;
}

static void *pepa_in_thread_watchdog(__attribute__((unused))void *arg)
{
	pepa_core_t *core             = pepa_get_core();

	PEPA_SOCK_LOCK(core->sockets.shva_rw, core);
	int         shva_socket_start = core->sockets.shva_rw;
	PEPA_SOCK_UNLOCK(core->sockets.shva_rw, core);

	do {
		PEPA_SOCK_LOCK(core->sockets.shva_rw, core);
		PEPA_SOCK_LOCK(core->sockets.in_listen, core
					  );
		int sock         = core->sockets.shva_rw;

		int shva_sock_st = pepa_test_fd(sock);
		int in_sock_st   = pepa_test_fd(core->sockets.in_listen);

		if ((PEPA_ERR_OK != shva_sock_st) || /* SHVA socket is bad */
			(PEPA_ERR_OK != in_sock_st) || /* IN socket is bad */
			(shva_socket_start != core->sockets.shva_rw)/* SHVA socket is changed */) {

			DE("IN WATCHDOG: Closing IN socket because: shva_sock_st = %d, in_sock_st = %d, shva_socket_start = %d, shva_socket now = %d\n",
			   shva_sock_st, in_sock_st, shva_socket_start, core->sockets.shva_rw);

			/* We do not close SHVA socket, the SHVA thread cares about it */
			pepa_in_thread_close_listen(core, __func__);
			PEPA_SOCK_UNLOCK(core->sockets.shva_rw, core);
			PEPA_SOCK_UNLOCK(core->sockets.in_listen, core);

			DE("SHVA or IN socket became invalid; closed listening socket\n");

			pthread_exit(NULL);
			usleep(1000);
		}

		PEPA_SOCK_UNLOCK(core->sockets.shva_rw, core);
		PEPA_SOCK_UNLOCK(core->sockets.in_listen, core);

	} while (1);
	return 0;
}

void *pepa_in_thread_new(__attribute__((unused))void *arg)
{
	pthread_t              pthread_id;
	int                    read_sock  = -1;
	pepa_in_thread_state_t next_step  = PEPA_TH_IN_START;
	const char             *my_name   = "IN";
	int                    rc;
	pepa_core_t            *core      = pepa_get_core();

	do {
		pepa_in_thread_state_t this_step = next_step;
		switch (next_step) {
			/*
			 * Start the thread
			 */
		case 	PEPA_TH_IN_START:
			DDD("START STEP: %s\n", pepa_in_thread_state_str(next_step));
			next_step = PEPA_TH_IN_CREATE_LISTEN;
			rc = pepa_pthread_init_phase(my_name);
			if (rc < 0) {
				DE("%s: Could not init the thread\n", my_name);
				pepa_state_set(core, PEPA_PR_IN, PEPA_ST_FAIL, __func__, __LINE__);
				next_step = PEPA_TH_IN_TERMINATE;
			}
			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_CREATE_LISTEN:
			/*
			 * Create Listening socket
			 */

			DDD("START STEP: %s\n", pepa_in_thread_state_str(next_step));
			next_step = PEPA_TH_IN_WAIT_SHVA;
			pepa_in_thread_listen_socket(core, my_name);
			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_CLOSE_LISTEN:
			DDD("START STEP: %s\n", pepa_in_thread_state_str(next_step));

			next_step = PEPA_TH_IN_CREATE_LISTEN;
			pepa_in_thread_close_listen(core,  my_name);

			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_TEST_LISTEN_SOCKET:
			DDD("START STEP: %s\n", pepa_in_thread_state_str(next_step));
			/*
			 * Test Listening socket; if it not valid,
			 * close the file descriptor and recreate the Listening socket
			 */

			next_step = PEPA_TH_IN_CREATE_WATCHDOG;
			if (pepa_test_fd(core->sockets.in_listen) < 0) {
				DE("IN: Listening socked is invalid, restart it\n");
				next_step = PEPA_TH_IN_CLOSE_LISTEN;
			} else {
				DE("IN: Listening socked is OK, reuse it\n");
			}
			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_CREATE_WATCHDOG:
			DDD("START STEP: %s\n", pepa_in_thread_state_str(this_step));
			/*
			 * Create a watchdog thread. This thread should test validity of the listening socket
			 * and the SHVA socket.
			 * If any of them becomes invalid, the watchdog thread closes Listening socket
			 * and terminate itself. It will be recreated on the next iteration of the state machine
			 */
			next_step = PEPA_TH_IN_ACCEPT;
			rc = pthread_create(&pthread_id, NULL, pepa_in_thread_watchdog, NULL);
			if (rc < 0) {
				DE("Can not create shva watchdog\n");
				next_step = PEPA_TH_IN_TERMINATE;
			}
			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_WAIT_SHVA:
			DDD("START STEP: %s\n", pepa_in_thread_state_str(next_step));
			/*
			 * Blocking wait until SHVA becomes available.
			 * This step happens before ACCEPT
			 */
			rc = pepa_in_thread_wait_shva(core, my_name);
			next_step = PEPA_TH_IN_ACCEPT;
			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_ACCEPT:
			DDD("START STEP: %s\n", pepa_in_thread_state_str(next_step));
			/*
			 * Wait until an income connection from the remote client
			 */

			/* TODO: Different steps depends on return status */
			read_sock = pepa_in_thread_listen(core, my_name);
			if (read_sock < 0) {
				DDE("Could not open listen socket\n");
				//usleep(1 * 1000000);
				next_step = PEPA_TH_IN_TEST_LISTEN_SOCKET;
				//next_step = PEPA_TH_IN_CLOSE_LISTEN;
			} else {
				DDD("Opened read socket: fd %d\n", read_sock);
				//core->sockets.in_listen = read_sock;
				next_step = PEPA_TH_IN_START_TRANSFER;
			}
			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_START_TRANSFER:
			DDD("START STEP: %s\n", pepa_in_thread_state_str(next_step));
			/*
			 * When any remote client is connected, a transfering thread for this
			 * new socket is created.
			 * This thread will read from this IN socket,
			 * and transfer data to SHVA socket.
			 * The SHVA socket is protected with a semaphore for this case.
			 * We do not care about this connection anymore;
			 * The transfering thread can detect that the IN socket became invalid,
			 * and it will terminate itself
			 */
			rc = pepa_in_thread_subthread(core, my_name, read_sock);
			if (rc < 0) {
				DE("Could not start subthread\n");
			}
			next_step = PEPA_TH_IN_ACCEPT;
			pepa_state_set(core, PEPA_PR_IN, PEPA_ST_RUN, __func__, __LINE__);
			next_step = PEPA_TH_IN_ACCEPT;
			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_TERMINATE:
			DDD("START STEP: %s\n", pepa_in_thread_state_str(next_step));
			pepa_state_set(core, PEPA_PR_IN, PEPA_ST_FAIL, __func__, __LINE__);
			sleep(10);
			DDD("END STEP:   %s\n", pepa_in_thread_state_str(this_step));
			break;

		default:
			DDD("Should never be here: next_steps = %d\n", next_step);
			abort();
			break;
		}
	} while (1);
	DE("Should never be here\n");
	abort();
	pthread_exit(NULL);

	return NULL;
}
