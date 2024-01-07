#if 0 /* SEB */
	#include <signal.h>
	#include <unistd.h>
	#include <errno.h>
	#include <sys/epoll.h>

	#include "slog/src/slog.h"
	#include "pepa_socket_common.h"
	#include "pepa_errors.h"
	#include "pepa_state_machine.h"

static void pepa_in_thread_close_listen(pepa_core_t *core, __attribute__((unused)) const char *my_name){
	if (core->sockets.in_listen < 0) {
		return;
	}

	pepa_socket_close_in_listen(core);
}

static void pepa_in_thread_listen_socket(pepa_core_t *core, __attribute__((unused)) const char *my_name){
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
static int32_t pepa_in_thread_wait_shva_ready(pepa_core_t *core, __attribute__((unused)) const char *my_name){
	slog_note_l("Start waiting SHVA UP");

	while (1) {
		int32_t st = pepa_state_shva_get(core);
		if (PEPA_ST_RUN == st) {
			slog_note_l("%s: SHVA became RUN: %d", my_name, st);
			return PEPA_ERR_OK;
		}

		slog_note_l("%s: SHVA is not RUN: [%d] %s", my_name, st, pepa_sig_str(st));

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note_l("IN GOT SIGNAL");
	};
	return PEPA_ERR_OK;
}

/* Wait for signal; when SHVA is DOWN, return */
static int32_t pepa_in_thread_wait_fail_event(pepa_core_t *core, __attribute__((unused)) const char *my_name){
	while (1) {
		uint32_t st = pepa_state_shva_get(core);
		if (PEPA_ST_FAIL == st) {
			slog_note_l("%s: SHVA is FAIL", my_name);
			return -PEPA_ERR_THREAD_SHVA_FAIL;
		}

		if (PEPA_ST_DOWN == st) {
			slog_note_l("%s: SHVA is DOWN", my_name);
			return -PEPA_ERR_THREAD_SHVA_DOWN;
		}

		st = pepa_state_in_get(core);

		if (PEPA_ST_FAIL == st) {
			slog_note_l("%s: IN became FAIL", my_name);
			return -PEPA_ERR_THREAD_IN_DOWN;
		}

		if (PEPA_ST_SOCKET_RESET == st) {
			slog_note_l("%s: IN must reset socket", my_name);
			return -PEPA_ERR_THREAD_IN_SOCKET_RESET;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note_l("IN GOT SIGNAL from IN: %s", pepa_sig_str(st));
	};
	return PEPA_ERR_OK;
}

	#define EMPTY_SLOT (-1)

static void pepa_in_reading_sockets_close_all(pepa_core_t *core){
	int i;
	slog_note_l("IN_FORWARD: Starting closing and removing sockets: %d slots", core->in_reading_sockets.number);
	for (i = 0; i < core->in_reading_sockets.number; i++) {
		if (EMPTY_SLOT != core->in_reading_sockets.sockets[i]) {

			//close(core->in_reading_sockets.sockets[i]);
			slog_note_l("Going to close in reading socket %d port %d",
						core->in_reading_sockets.sockets[i],
						pepa_find_socket_port(core->in_reading_sockets.sockets[i]));
			pepa_reading_socket_close(core->in_reading_sockets.sockets[i], "IN FORWARD READ");
			slog_note_l("IN_FORWARD: Closed socket %d in slot %d", core->in_reading_sockets.sockets[i], i);
			core->in_reading_sockets.sockets[i] = EMPTY_SLOT;
		}
	}
	slog_note_l("IN_FORWARD: Finished closing and removing sockets: %d slots", core->in_reading_sockets.number);
}

static void pepa_in_reading_sockets_free(pepa_core_t *core){
	slog_note_l("IN_FORWARD: Starting socket closing and cleaning");
	pepa_in_reading_sockets_close_all(core);
	free(core->in_reading_sockets.sockets);
	core->in_reading_sockets.sockets = NULL;
	slog_note_l("IN_FORWARD: Finished socket closing and cleaning");
}

static void pepa_in_reading_sockets_allocate(pepa_core_t *core, int num){
	int i;
	core->in_reading_sockets.number = num;
	core->in_reading_sockets.sockets = malloc(sizeof(int) * num);
	for (i = 0; i < num; i++) {
		core->in_reading_sockets.sockets[i] = EMPTY_SLOT;
	}
	slog_note_l("IN_FORWARD: Allocated %d socket slots", num);
}

static void pepa_in_reading_sockets_add(pepa_core_t *core, int fd){
	int i;
	for (i = 0; i < core->in_reading_sockets.number; i++) {
		if (EMPTY_SLOT == core->in_reading_sockets.sockets[i]) {
			core->in_reading_sockets.sockets[i] = fd;
			slog_note_l("IN_FORWARD: Added socket %d to slot %d", core->in_reading_sockets.sockets[i], i);
			return;
		}
	}
}

static void pepa_in_reading_sockets_close_rm(pepa_core_t *core, int fd){
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

	#define MAX_CLIENTS (1024)

void pepa_in_thread_new_forward_clean(void *arg){

	pepa_core_t         *core       = pepa_get_core();
	// int32_t         *epoll_fd = arg;
	char                *my_name    = "IN-FORWARD-CLEAN";

	thread_clean_args_t *clean_args = arg;

	slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_warn("%s: Starting clean", my_name);


	if (clean_args->epoll_fd >= 0) {
		close(clean_args->epoll_fd);
	}

	if (NULL != clean_args->buf) {
		free(clean_args->buf);
	}

	pepa_in_reading_sockets_free(core);
	//pepa_in_reading_sockets_close_all(core);
	/* Set state: we failed */
	pepa_state_in_set(core, PEPA_ST_SOCKET_RESET);

	slog_warn("%s: Finished clean", my_name);
	slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

int32_t pepa_in_epoll_test_hang_up(pepa_core_t *core, int32_t epoll_fd, struct epoll_event events[], int32_t num_events){
	int32_t i;
	for (i = 0; i < num_events; i++) {
		/* If no hung ups - continue */
		if (!(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
			//if (!(events[i].events & (EPOLLRDHUP | EPOLLHUP))) {
			continue;
		}

		/* If listening socket is down, return error and the trhead will be terminated  */
		if (core->sockets.in_listen == events[i].data.fd) {
			slog_warn_l("IN Listening socket: disconnected");
			return -PEPA_ERR_SOCKET_IN_LISTEN_DOWN;
		}

		/* Other case, just remove the failen socket from the set */

		int32_t rc  = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
		int32_t err = errno;

		slog_warn_l("IN reading socket: disconnected external writer, fd: %d", events[i].data.fd);

		if (0 != rc) {
			slog_debug_l("Can not remove file %d descriptor from the epoll set: %s", events[i].data.fd, strerror(err));
		}

		/* Close the file descriptor */
		// rc = close(events[i].data.fd);
		pepa_in_reading_sockets_close_rm(core, events[i].data.fd);

	#if 0 /* SEB */
		if (0 != rc) {
			slog_debug_l("Can not close file descriptor from the epoll set: %s", strerror(err));
		}
	#endif
	}

	return PEPA_ERR_OK;
}

static int32_t pepa_in_accept_new_connection(pepa_core_t *core, int32_t epoll_fd){
	struct sockaddr_in address;
	int32_t            new_socket = -1;
	static int32_t     addrlen    = sizeof(address);

	if ((new_socket = accept(core->sockets.in_listen,
							 (struct sockaddr *)&address,
							 (socklen_t *)&addrlen)) < 0) {
		slog_error_l("%s: Error on accept: %s", "IN-FORWARD", strerror(errno));
		return -1;
	}

	// pepa_set_tcp_connection_props(core, new_socket);
	pepa_set_tcp_timeout(new_socket);
	pepa_set_tcp_recv_size(core, new_socket);
	const int enable = 1;
	int rc = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (rc < 0) {
		slog_error_l("Open Socket [from %s]: Could not set SO_REUSEADDR on socket, error: %s", "IN FORWARD", strerror(errno));
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	if (0 != epoll_ctl_add(epoll_fd, new_socket, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		slog_error_l("%s: Can not add new socket to epoll set: %s", "IN-FORWARD", strerror(errno));
		pepa_reading_socket_close(new_socket, "IN FORWARD READ");
		return -1;
	}

	pepa_in_reading_sockets_add(core, new_socket);

	slog_warn_l("%s: Added new socket %d to epoll set", "IN-FORWARD", new_socket);
	return PEPA_ERR_OK;
}

int32_t pepa_in_process_buffers(pepa_core_t *core, int32_t epoll_fd, char *buffer, struct epoll_event events[], int32_t num_events){
	int32_t rc        = PEPA_ERR_OK;
	int32_t ret       = PEPA_ERR_OK;
	int32_t rc_remove;
	int32_t i;

	for (i = 0; i < num_events; i++) {
		if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
			// if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
			slog_warn_l("%s: An exception detected on sockt %d", "IN-FORWARD", events[i].data.fd);
			pepa_in_reading_sockets_close_rm(core, events[i].data.fd);
			continue;
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
										  /* Max iterations */ 1);
			pepa_shva_socket_unlock(core);

			if (PEPA_ERR_OK == rc) {
				//slog_warn_l("%s: Sent from socket %d", "IN-FORWARD", events[i].data.fd);
				continue;
			}

			ret = -1;


			slog_note_l("An error on sending buffers: %s", pepa_error_code_to_str(rc));

			/* Something wrong with the socket, should be removed */

			/* Writing side is off, means: SHVA socket is invalid */
			if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
				slog_note_l("Could not write to SHVA; setting SHVA to FAIL");
				pepa_state_shva_set(core, PEPA_ST_FAIL);
				return ret;
			}

			slog_warn_l("%s: Could not send from socket %d", "IN-FORWARD", events[i].data.fd);
			rc_remove = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
			pepa_in_reading_sockets_close_rm(core, events[i].data.fd);
			//close(events[i].data.fd);

			if (rc_remove) {
				slog_warn_l("%s: Could not remove socket %d from epoll set", "IN-FORWARD", events[i].data.fd);
			}

		} /* End of read descriptor processing */
	}
	return ret;
}

	#define EVENTS_NUM (10)

void *pepa_in_thread_new_forward(__attribute__((unused))void *arg){
	thread_clean_args_t clean_args;
	memset(&clean_args, 9, sizeof(thread_clean_args_t));
	clean_args.epoll_fd = -1;
	pthread_cleanup_push(pepa_in_thread_new_forward_clean, &clean_args);
	int32_t            rc;
	pepa_core_t        *core              = pepa_get_core();
	char               *my_name           = "IN-FORWARD";
	//char               buffer[BUF_SIZE];  //data buffer of 1K
	char               *buffer;  //data buffer of 1K
	struct epoll_event events[EVENTS_NUM];


	int32_t            epoll_fd           = epoll_create1(EPOLL_CLOEXEC);

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

	pepa_in_reading_sockets_allocate(core, MAX_CLIENTS);

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

		int32_t event_count = epoll_wait(epoll_fd, events, EVENTS_NUM, 1);

		/* Interrupted by a signal */
		if (event_count < 0 && EINTR == errno) {
			continue;
		}

		/* An error happened, we just terminate the thread */
		if (event_count < 0) {
			slog_fatal_l("%s: error on wait: %s", my_name, strerror(errno));
			close(epoll_fd);
			//pepa_state_in_set(core, PEPA_ST_SOCKET_RESET);
			pthread_exit(NULL);
		}

		rc = pepa_in_epoll_test_hang_up(core, epoll_fd, events, event_count);

		/* We get error if the listening socket is diconnected  */
		if (PEPA_ERR_OK != rc) {
			//pepa_state_in_set(core, PEPA_ST_SOCKET_RESET);
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

void pepa_in_thread_clean(void *arg){
	pepa_core_t         *core       = pepa_get_core();
	// int32_t         *epoll_fd = arg;
	char                *my_name    = "IN-CLEAN";

	thread_clean_args_t *clean_args = arg;

	slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_warn("%s: Starting clean", my_name);

	pepa_thread_kill_in_fw(core);
	pepa_in_thread_close_listen(core, my_name);
	pepa_state_in_set(core, PEPA_ST_DOWN);

	slog_warn("%s: Finished clean", my_name);
	slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

void *pepa_in_thread(__attribute__((unused))void *arg){
	pthread_cleanup_push(pepa_in_thread_clean , NULL);
	pepa_in_thread_state_t next_step = PEPA_TH_IN_START;
	const char             *my_name  = "IN";
	int32_t                rc;
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
				pepa_state_in_set(core, PEPA_ST_DOWN);
				next_step = PEPA_TH_IN_TERMINATE;
			}
			pepa_state_in_set(core, PEPA_ST_DOWN);
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

			case PEPA_TH_IN_CREATE_LISTEN:

			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));
			next_step = PEPA_TH_IN_WAIT_SHVA_UP;
			pepa_in_thread_listen_socket(core, my_name);
			slog_note_l("END STEP:   %s", pepa_in_thread_state_str(this_step));
			break;

			case PEPA_TH_IN_CLOSE_LISTEN:
			slog_note_l("START STEP: %s", pepa_in_thread_state_str(next_step));

			next_step = PEPA_TH_IN_CREATE_LISTEN;
			pepa_thread_kill_in_fw(core);
			pepa_in_thread_close_listen(core,  my_name);
			//usleep(1000);
			pepa_state_in_set(core, PEPA_ST_DOWN);

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

			/* SHVA is not ready, continue waiting */
			if (-PEPA_ERR_THREAD_SHVA_DOWN == rc) {
				next_step = PEPA_TH_IN_WAIT_SHVA_UP;
			}

			/* SHVA is fail, IN must reset itself */
			if (-PEPA_ERR_THREAD_SHVA_FAIL == rc) {
				next_step = PEPA_TH_IN_CLOSE_LISTEN;
				//pepa_thread_kill_in_fw(core);
			}

			/* This process, IN thread, is fail and must reset itself */
			if (-PEPA_ERR_THREAD_IN_FAIL == rc) {
				next_step = PEPA_TH_IN_CLOSE_LISTEN;
				//pepa_thread_kill_in_fw(core);
			}

			/* This process, IN thread, must close socket and reopen it */
			if (-PEPA_ERR_THREAD_IN_SOCKET_RESET == rc) {
				//next_step = PEPA_TH_IN_WAIT_SHVA_UP;
				next_step = PEPA_TH_IN_CLOSE_LISTEN;
			}

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

	pthread_cleanup_pop(0);
	pthread_exit(NULL);

	return NULL;
}
#endif

