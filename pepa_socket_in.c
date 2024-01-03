#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

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

static void pepa_in_thread_listen_socket(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	struct sockaddr_in s_addr;

	while (1) {
		//slog_note_l("%s: Opening listening socket", my_name);
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
		usleep(1000);
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
			slog_note_l("%s: IN required to reset socket", my_name);
			return -PEPA_ERR_THREAD_IN_SOCKET_RESET;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note_l("IN GOT SIGNAL");
	};
	return PEPA_ERR_OK;
}

#define MAX_CLIENTS (512)

void pepa_in_thread_new_forward_clean(void *arg)
{

	pepa_core_t         *core       = pepa_get_core();
	// int         *epoll_fd = arg;
	char                *my_name    = "IN-FORWARD-CLEAN";

	thread_clean_args_t *clean_args = arg;

	slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_warn_l("%s: Starting clean", my_name);

	close(clean_args->epoll_fd);
	free(clean_args->buf);
	/* Set state: we failed */
	pepa_state_in_set(core, PEPA_ST_SOCKET_RESET);

	slog_warn_l("%s: Finished clean", my_name);
	slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

int pepa_in_epoll_test_hang_up(pepa_core_t *core, int epoll_fd, struct epoll_event events[], int num_events)
{
	int i;
	for (i = 0; i < num_events; i++) {
		/* If no hung ups - continue */
		//if (!(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
		if (!(events[i].events & (EPOLLRDHUP | EPOLLHUP))) {
			continue;
		}

		/* If one of the read/write sockets is diconnected, exit the thread */

		if (core->sockets.in_listen == events[i].data.fd) {
			slog_warn_l("IN Listening socket: disconnected");
			pepa_state_shva_set(core, PEPA_ST_FAIL);
			return -PEPA_ERR_SOCKET_IN_LISTEN_DOWN;
		}

		/* Other case, just remove the failen socket from the set */

		int rc  = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
		int err = errno;
		slog_warn_l("IN reading socket: disconnected external writer, fd: %d", events[i].data.fd);

		if (0 != rc) {
			slog_debug_l("Can not remove file descriptor from the epoll set: %s", strerror(err));
		}

		/* Close the file descriptor */
		rc = close(events[i].data.fd);

		if (0 != rc) {
			slog_debug_l("Can not close file descriptor from the epoll set: %s", strerror(err));
		}
	}

	return PEPA_ERR_OK;
}

int pepa_in_accept_new_connection(pepa_core_t *core, int epoll_fd)
{
	struct sockaddr_in address;
	int                new_socket = -1;
	static int         addrlen    = sizeof(address);

	if ((new_socket = accept(core->sockets.in_listen,
							 (struct sockaddr *)&address,
							 (socklen_t *)&addrlen)) < 0) {
		slog_error_l("%s: Error on accept: %s", "IN-FORWARD", strerror(errno));
		return -1;
	}

	// pepa_set_tcp_connection_props(core, new_socket);
	pepa_set_tcp_timeout(new_socket);
	pepa_set_tcp_recv_size(core, new_socket);

	if (0 != epoll_ctl_add(epoll_fd, new_socket, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		slog_error_l("%s: Can not add new socke to epoll set: %s", "IN-FORWARD", strerror(errno));
		return -1;
	}

	slog_warn_l("%s: Added new socket %d to epoll set", "IN-FORWARD", new_socket);

	return PEPA_ERR_OK;
}

int pepa_in_process_buffers(pepa_core_t *core, int epoll_fd, char *buffer, struct epoll_event events[], int num_events)
{

	int rc        = PEPA_ERR_OK;
	int ret       = PEPA_ERR_OK;
	int rc_remove;
	int i;

	for (i = 0; i < num_events; i++) {
		if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
			slog_warn_l("%s: An exception detected on sockt %d", "IN-FORWARD", events[i].data.fd);
		}

		/* Read /write from/to socket */
		if (events[i].events & EPOLLIN) {

			/* The special socket: listening, if there is an event, we should to open a new connection */
			if (core->sockets.in_listen == events[i].data.fd) {
				rc = pepa_in_accept_new_connection(core, epoll_fd);

				/* If somethins happened during this process, we stop and return */
				if (PEPA_ERR_OK != rc) {
					return rc;
				}
				continue;
			}

#if 0 /* SEB */
			rc = pepa_one_direction_copy2(/* Send to : */core->sockets.shva_rw, "SHVA",
										  /* From: */ events[i].data.fd, "IN READ",
										  buffer, core->internal_buf_size * 1024, /*Debug is ON */ 1,
										  /* RX stat */&core->monitor.in_rx,
										  /* TX stat */&core->monitor.shva_tx);
#endif	

			/* We must lock SHVA socket since IN can run several instances of this thread */
			pepa_shva_socket_lock(core);
			rc = pepa_one_direction_copy3(/* Send to : */core->sockets.shva_rw, "SHVA",
										  /* From: */ events[i].data.fd, "IN READ",
										  buffer, core->internal_buf_size * 1024, /*Debug is ON */ 1,
										  /* RX stat */&core->monitor.in_rx,
										  /* TX stat */&core->monitor.shva_tx,
										  /* Max iterations */ 4);
			pepa_shva_socket_unlock(core);

			if (PEPA_ERR_OK == rc) {
				//slog_warn_l("%s: Sent from socket %d", "IN-FORWARD", events[i].data.fd);
				continue;
			}


			ret = -1;

			/* Something wrong with the cosket, should be removed */

			slog_warn_l("%s: Could not send from socket %d", "IN-FORWARD", events[i].data.fd);
			rc_remove = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
			close(events[i].data.fd);

			if (rc_remove) {
				slog_warn_l("%s: Could not remove socket %d from epoll set", "IN-FORWARD", events[i].data.fd);
			}

		} /* End of read descriptor processing */
	}
	return ret;
}

#define EVENTS_NUM (10)

void *pepa_in_thread_new_forward(__attribute__((unused))void *arg)
{
	int                 rc;
	pepa_core_t         *core              = pepa_get_core();
	char                *my_name           = "IN-FORWARD";
	//char               buffer[BUF_SIZE];  //data buffer of 1K
	char                *buffer;  //data buffer of 1K
	struct epoll_event  events[EVENTS_NUM];

	thread_clean_args_t clean_args;

	int                 epoll_fd           = epoll_create1(EPOLL_CLOEXEC);

	if (epoll_fd < 0) {
		slog_error_l("Can not open epoll fd");
		pthread_exit(NULL);
	}

	buffer = calloc(core->internal_buf_size * 1024, 1);

	if (NULL == buffer) {
		slog_error_l("Can not allocate internal buffer, terminating thread");
		close(epoll_fd);
		pthread_exit(NULL);
	}

	clean_args.buf = buffer;
	clean_args.epoll_fd = epoll_fd;

	pthread_cleanup_push(pepa_in_thread_new_forward_clean, &clean_args);

	rc = pepa_pthread_init_phase(my_name);
	if (rc < 0) {
		slog_fatal_l("%s: Could not init the thread", my_name);
		pthread_exit(NULL);
	}

	if (0 != epoll_ctl_add(epoll_fd, core->sockets.in_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		close(epoll_fd);
		slog_fatal_l("%s: Could not add listening socket to epoll", my_name);
		pthread_exit(NULL);
	}

	slog_note_l("%s: Waiting for connections", my_name);

	//pepa_state_in_fw_set(core, PEPA_ST_RUN);

	while (1)   {

		int event_count = epoll_wait(epoll_fd, events, EVENTS_NUM, 100);

		/* Interrupted by a signal */
		if (event_count < 0 && EINTR == errno) {
			continue;
		}

		/* An error happened, we just terminate the thread */
		if (event_count < 0) {
			slog_fatal_l("%s: error on wait: %s", my_name, strerror(errno));
			close(epoll_fd);
			pepa_state_out_set(core, PEPA_ST_FAIL);
			pthread_exit(NULL);
		}

		rc = pepa_in_epoll_test_hang_up(core, epoll_fd, events, event_count);

		/* We get error if the listening socket is diconnected  */
		if (PEPA_ERR_OK != rc) {
			pthread_exit(NULL);
		}

		/* Now run on the sockets that received an event and transfer buffers / add new connections */

		rc = pepa_in_process_buffers(core, epoll_fd, buffer, events, event_count);

		/* We get error if the listening socket is diconnected  */
		if (PEPA_ERR_OK != rc) {
			//slog_warn_l("An error in socket; continue");
			//pthread_exit(NULL);
		}
	}

	pthread_cleanup_pop(0);
	pthread_exit(NULL);
}

void *pepa_in_thread(__attribute__((unused))void *arg)
{
	pepa_in_thread_state_t next_step = PEPA_TH_IN_START;
	const char             *my_name  = "IN";
	int                    rc;
	pepa_core_t            *core     = pepa_get_core();

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

			next_step = PEPA_TH_IN_WAIT_SHVA_UP;

			if (-PEPA_ERR_THREAD_SHVA_DOWN == rc) {
				next_step = PEPA_TH_IN_WAIT_SHVA_UP;
			}

			if (-PEPA_ERR_THREAD_IN_DOWN == rc) {
				next_step = PEPA_TH_IN_CLOSE_LISTEN;
			}

			if (-PEPA_ERR_THREAD_IN_SOCKET_RESET == rc) {
				//next_step = PEPA_TH_IN_WAIT_SHVA_UP;
				next_step = PEPA_TH_IN_CLOSE_LISTEN;
			}
			pepa_thread_kill_in_fw(core);

			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_START_TRANSFER:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));

#if 0 /* SEB */
			rc = pthread_create(&forwarder_pthread_id, NULL, pepa_in_thread_new_forward, NULL);
			if (rc < 0) {
				slog_fatal_l("Could not start subthread");
			}
#endif
			pepa_thread_start_in_fw(core);
			pepa_state_in_set(core, PEPA_ST_RUN);
			next_step = PEPA_TH_IN_WAIT_SHVA_DOWN;
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

		case PEPA_TH_IN_TERMINATE:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));
#if 0 /* SEB */
			rc = pthread_cancel(forwarder_pthread_id);
			if (rc < 0) {
				slog_warn_l("%s: Could not terminate forwarding thread: %s", my_name, strerror(errno));
			}
#endif
			pepa_thread_kill_in_fw(core);
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

