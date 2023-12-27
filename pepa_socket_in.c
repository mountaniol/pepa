#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "slog/src/slog.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_state_machine.h"

static void pepa_in_thread_close_listen(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	if (core->sockets.in_listen < 0) {
		return;
	}

	pepa_socket_close_in_listen(core);
}

static void pepa_in_thread_listen_socket(pepa_core_t *core, const char *my_name)
{
	struct sockaddr_in s_addr;

	while (1) {
		slog_note_l("%s: Opening listening socket", my_name);
		/* Just try to close it */
		pepa_in_thread_close_listen(core, __func__);

		core->sockets.in_listen = pepa_open_listening_socket(&s_addr,
															 core->in_thread.ip_string,
															 core->in_thread.port_int,
															 core->in_thread.clients,
															 __func__);
		if (core->sockets.in_listen >= 0) {
			return;
		}
		sleep(3);
	}
}

/* Wait for signal; when SHVA is UP, return */
static int pepa_in_thread_wait_shva_ready(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	slog_note_l("Start waiting SHVA UP");

	while (1) {
		int st = pepa_state_shva_get(core);
		if (PEPA_ST_RUN == st) {
			slog_note_l("%s: SHVA became UP: %d", my_name, st);
			return PEPA_ERR_OK;
		}

		slog_note_l("%s: SHVA is not UP: [%d] %s", my_name, st, pepa_sig_str(st));

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note_l("IN GOT SIGNAL");
	};
	return PEPA_ERR_OK;
}

/* Wait for signal; when SHVA is DOWN, return */
static int pepa_in_thread_wait_fail_event(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	while (1) {
		if (PEPA_ST_RUN != pepa_state_shva_get(core)) {
			slog_note_l("%s: SHVA became DOWN", my_name);
			return -PEPA_ERR_THREAD_SHVA_DOWN;
		}

		int st = pepa_state_in_get(core);
		if (PEPA_ST_FAIL == st) {
			slog_note_l("%s: IN became DOWN", my_name);
			return -PEPA_ERR_THREAD_IN_DOWN;
		}

		if (PEPA_ST_SOCKET_RESET == st) {
			slog_note_l("%s: IN became DOWN", my_name);
			return -PEPA_ERR_THREAD_IN_SOCKET_RESET;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note_l("IN GOT SIGNAL");
	};
	return PEPA_ERR_OK;
}

#define MAX_CLIENTS (512)
#define BUF_SIZE (2048)
#define EMPTY_SLOT (-1)

void pepa_in_thread_new_forward_clean(void *arg)
{
	pepa_core_t *core          = pepa_get_core();
	int         i;
	int         *client_socket = (int *)arg;
	char        *my_name       = "IN-FORWARD-CLEAN";
	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_note_l("%s: Starting clean", my_name);
	//initialise all client_socket[] to 0 so not checked
	for (i = 0; i < MAX_CLIENTS; i++)   {
		if (EMPTY_SLOT != client_socket[i]) {
			close(client_socket[i]);
			slog_note_l("%s: Closed fd: %d", my_name, client_socket[i]);
		}
	}
	/* Set state: we failed */
	pepa_state_in_set(core, PEPA_ST_FAIL);
	slog_note_l("%s: Finished clean", my_name);
	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

void *pepa_in_thread_new_forward(__attribute__((unused))void *arg)
{
	int                rc;
	pepa_core_t        *core                      = pepa_get_core();
	char               *my_name                   = "IN-FORWARD";

	int                new_socket,
					   activity,
					   i,
					   sd;
	int                max_sd;
	struct sockaddr_in address;

	int collected_error = 0;

	char               buffer[BUF_SIZE];  //data buffer of 1K
	int                *client_socket   = calloc(sizeof(int) * MAX_CLIENTS, 1);
	if (NULL == client_socket) {
		slog_fatal_l("Can not allocate file descriptors array");
		pthread_exit(NULL);
	}

	//set of socket descriptors
	fd_set             readfds;
	struct timeval     tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	//initialise all client_socket[] to 0 so not checked
	for (i = 0; i < MAX_CLIENTS; i++)   {
		client_socket[i] = EMPTY_SLOT;
	}

	rc = pepa_pthread_init_phase(my_name);
	if (rc < 0) {
		slog_fatal_l("%s: Could not init the thread", my_name);
		pthread_exit(NULL);
	}

	pthread_cleanup_push(pepa_in_thread_new_forward_clean, client_socket);

	//accept the incoming connection
	const int addrlen = sizeof(address);
	slog_note_l("%s: Waiting for connections", my_name);

	while (1)   {

		/* If 4 times in row we cant read from a socket, it is time to reset listening socket */
		if (collected_error > 4) {
			pepa_state_in_set(core, PEPA_ST_SOCKET_RESET);
			pthread_exit(NULL);
		}
		//slog_note("Starting new iteration");
		//clear the socket set
		FD_ZERO(&readfds);

		//add master socket to set
		FD_SET(core->sockets.in_listen, &readfds);
		max_sd = core->sockets.in_listen;

		//add child sockets to set
		for (i = 0; i < MAX_CLIENTS; i++)   {
			//socket descriptor
			if (EMPTY_SLOT != client_socket[i]) {
				FD_SET(client_socket[i], &readfds);
				// slog_note_l("Added fd %d from slot %d to set", client_socket[i], i);

				if (client_socket[i] > max_sd) {
					max_sd = client_socket[i];
				}
			}
		} /* for (i = 0; i < MAX_CLIENTS; i++) */

		//wait for an activity on one of the sockets , timeout is NULL ,
		//so wait indefinitely
		activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);

		/* Interrupted by a signal, continue */
		if ((activity < 0) && (errno != EINTR)) {
			slog_fatal_l("%s: select error: %s", my_name, strerror(errno));
			pthread_exit(NULL);
		}

		//If something happened on the master socket ,
		//then its an incoming connection
		if (FD_ISSET(core->sockets.in_listen, &readfds)) {
			if ((new_socket = accept(core->sockets.in_listen,
									 (struct sockaddr *)&address,
									 (socklen_t *)&addrlen)) < 0) {
				slog_error_l("%s: Error on accept: %s", my_name, strerror(errno));
				pthread_exit(NULL);
			} /* accept() */

			//inform user of socket number - used in send and receive commands
			slog_note_l("%s: New connection, socket fd is %d", my_name, new_socket);

			//add new socket to array of sockets
			for (i = 0; i < MAX_CLIENTS; i++)   {
				//if position is empty
				if (EMPTY_SLOT == client_socket[i]) {
					client_socket[i] = new_socket;
					// slog_note_l("%s: Adding a new socket to the list of sockets as %d", my_name, i);
					break;
				} /* if() */
			} /* for() */
			continue;
		} /* if (FD_ISSET(core->sockets.out_listen, &readfds)) */

		//else its some IO operation on some other socket
		for (i = 0; i < MAX_CLIENTS; i++)   {
			sd = client_socket[i];
			if (EMPTY_SLOT == sd || (!FD_ISSET(sd, &readfds))) {
				continue;
			}

			// slog_note_l("%s: there is something on socket %d (slot %d)", my_name, sd, i);

			rc = pepa_one_direction_copy2(/* Send to : */core->sockets.shva_rw, "SHVA",
										  /* From: */ sd, "IN", buffer, BUF_SIZE, /*Debug is ON */ 1,
										  /* RX stat */&core->monitor.in_rx,
										  /* TX stat */&core->monitor.shva_tx);
			if (PEPA_ERR_OK == rc) {
				// slog_note_l("%s: Sent to SHVA OK, collected error: %d", my_name, collected_error);
				if (collected_error > 0) {
					collected_error--;
				}
				continue;
			} /* PEPA_ERR_OK */

			if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
				collected_error++;
				slog_note_l("%s: Could read from IN socket [fd: %d], closing read IN socket, collected error: %d", my_name, sd, collected_error);
				close(sd);
				client_socket[i] = EMPTY_SLOT;
				break;
			} /* -PEPA_ERR_BAD_SOCKET_READ */

			if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
				slog_note("%s: Could write to SHVA socket [fd: %d], exiting thread", my_name, core->sockets.shva_rw);
				pthread_exit(NULL);
			} /* -PEPA_ERR_BAD_SOCKET_READ */
		} /* MAX_CLIENTS */
	}

	pthread_cleanup_pop(0);
	pthread_exit(NULL);
}

void *pepa_in_thread(__attribute__((unused))void *arg)
{
	pthread_t              forwarder_pthread_id = 0xDEADBEED;
	//int                    read_sock            = -1;
	pepa_in_thread_state_t next_step            = PEPA_TH_IN_START;
	const char             *my_name             = "IN";
	int                    rc;
	pepa_core_t            *core                = pepa_get_core();

	set_sig_handler();

	do {
		pepa_in_thread_state_t this_step = next_step;
		switch (next_step) {
			/*
			 * Start the thread
			 */
		case 	PEPA_TH_IN_START:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));
			next_step = PEPA_TH_IN_CREATE_LISTEN;
			rc = pepa_pthread_init_phase(my_name);
			if (rc < 0) {
				slog_fatal_l("%s: Could not init the thread", my_name);
				pepa_state_in_set(core, PEPA_ST_FAIL);
				next_step = PEPA_TH_IN_TERMINATE;
			}
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_CREATE_LISTEN:
			/*
			 * Create Listening socket
			 */

			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));
			next_step = PEPA_TH_IN_WAIT_SHVA_UP;
			pepa_in_thread_listen_socket(core, my_name);
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_CLOSE_LISTEN:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));

			next_step = PEPA_TH_IN_CREATE_LISTEN;
			pepa_in_thread_close_listen(core,  my_name);

			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_TEST_LISTEN_SOCKET:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));
			/*
			 * Test Listening socket; if it not valid,
			 * close the file descriptor and recreate the Listening socket
			 */

			next_step = PEPA_TH_IN_WAIT_SHVA_UP;
			if (pepa_test_fd(core->sockets.in_listen) < 0) {
				slog_error_l("%s: Listening socked is invalid, restart it", my_name);
				next_step = PEPA_TH_IN_CLOSE_LISTEN;
			} else {
				slog_note_l("%s: IN: Listening socked is OK, reuse it", my_name);
			}
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_WAIT_SHVA_UP:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));
			/*
			 * Blocking wait until SHVA becomes available.
			 * This step happens before ACCEPT
			 */
			rc = pepa_in_thread_wait_shva_ready(core, my_name);

			next_step = PEPA_TH_IN_START_TRANSFER;
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_WAIT_SHVA_DOWN:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));
			/*
			 * Blocking wait until SHVA is DOWN.
			 * While SHVA is alive, we run transfer between IN and SHVA 
			 */
			rc = pepa_in_thread_wait_fail_event(core, my_name);

			if (0 == pthread_kill(forwarder_pthread_id, 0)) {
				rc = pthread_cancel(forwarder_pthread_id);
			}
			next_step = PEPA_TH_IN_WAIT_SHVA_UP;

			if (2 == rc) {
				slog_warn("%s: Could not terminate forwarding thread: %s", my_name, strerror(errno));
				next_step = PEPA_TH_IN_CLOSE_LISTEN;
			}

			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_START_TRANSFER:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));

			rc = pthread_create(&forwarder_pthread_id, NULL, pepa_in_thread_new_forward, NULL);
			if (rc < 0) {
				slog_fatal_l("Could not start subthread");
			}
			pepa_state_in_set(core, PEPA_ST_RUN);
			next_step = PEPA_TH_IN_WAIT_SHVA_DOWN;
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_TERMINATE:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));
			rc = pthread_cancel(forwarder_pthread_id);
			if (rc < 0) {
				slog_warn_l("%s: Could not terminate forwarding thread: %s", my_name, strerror(errno));
			}
			pepa_state_in_set(core, PEPA_ST_FAIL);
			sleep(10);
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		default:
			slog_note_l("%s: Should never be here: next_steps = %d", my_name, next_step);
			abort();
			break;
		}
	} while (1);
	slog_fatal_l("Should never be here");
	abort();
	pthread_exit(NULL);

	return NULL;
}

