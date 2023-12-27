#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

#include "slog/src/slog.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_state_machine.h"

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

static int pepa_shva_thread_open_connection(pepa_core_t *core, const char *my_name)
{
	/* Open connnection to the SHVA server */
	do {
		core->sockets.shva_rw = pepa_open_shava_connection();

		if (core->sockets.shva_rw < 0) {
			core->sockets.shva_rw = -1;
			slog_note_l("%s: Can not open connection to SHVA server; wait 1 seconds and try over", my_name);
			usleep(1 * 1000000);
			continue;
		}
	} while (core->sockets.shva_rw < 0);

	slog_note_l("%s: Opened connection to SHVA", my_name);
	return PEPA_ERR_OK;
}

static int pepa_shva_thread_wait_out(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	while (1) {
		if (PEPA_ST_RUN == pepa_state_out_get(core)) {
			slog_note_l("%s: OUT is UP", my_name);
			return PEPA_ERR_OK;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note_l("SHVA GOT SIGNAL");
	};
	return PEPA_ERR_OK;
}

/* Wait for signal; when SHVA is DOWN, return */
static int pepa_shva_thread_wait_fail(pepa_core_t *core, __attribute__((unused)) const char *my_name)
{
	while (1) {
		if (PEPA_ST_FAIL == pepa_state_out_get(core)) {
			slog_note_l("%s: OUT became DOWN", my_name);
			return -PEPA_ERR_THREAD_OUT_DOWN;
		}

		if (PEPA_ST_FAIL == pepa_state_shva_get(core)) {
			slog_note_l("%s: SHVA became DOWN", my_name);
			return -PEPA_ERR_THREAD_SHVA_DOWN;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note_l("SHVA GOT SIGNAL");
	};
	return PEPA_ERR_OK;
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

	set_sig_handler();

	do {
		pepa_shva_thread_state_t this_step = next_step;
		switch (next_step) {
		case 	PEPA_TH_SHVA_START:
			slog_note_l("START STEP: %s", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			rc = pepa_pthread_init_phase(my_name);
			if (rc < 0) {
				slog_fatal_l("%s: Could not init the thread", my_name);
				pepa_state_shva_set(core, PEPA_ST_FAIL);
				next_step = PEPA_TH_SHVA_TERMINATE;
			}
			slog_note_l("END STEP:   %s", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_OPEN_CONNECTION:
			slog_note_l("START STEP: %s", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_WAIT_OUT;
			rc = pepa_shva_thread_open_connection(core, my_name);
			if (PEPA_ERR_OK != rc) {
				next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			} else {
				/* By this signal IN thread should start transfer data */
				pepa_state_shva_set(core, PEPA_ST_RUN);
				slog_note_l("%s: SHVA set itself UP", my_name);
			}
			slog_note_l("END STEP:   %s", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_WAIT_OUT:
			slog_note_l("START STEP: %s", pepa_shva_thread_state_str(this_step));
			next_step = PEPA_TH_SHVA_START_TRANSFER;
			rc = pepa_shva_thread_wait_out(core, my_name);
			if (PEPA_ERR_OK != rc) {
				next_step = PEPA_TH_SHVA_WAIT_OUT;
			}
			slog_note_l("END STEP:   %s", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_START_TRANSFER:
			slog_note_l("START STEP: %s", pepa_shva_thread_state_str(this_step));
			/* TODO: Different steps depends on return status */
			// rc = pepa_shva_thread_start_transfer(core, my_name);
			rc = pthread_create(&core->shva_forwarder.thread_id, NULL, pepa_shva_thread_new_forward, NULL);
			if (rc < 0) {
				slog_warn_l("%s: Could not start listening socket", my_name);
				next_step = PEPA_TH_SHVA_CLOSE_SOCKET;
			}
			next_step = PEPA_TH_SHVA_WATCH_SOCKET;
			slog_note_l("END STEP:   %s", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_WATCH_SOCKET:
			slog_note_l("START STEP: %s", pepa_shva_thread_state_str(next_step));
			next_step = PEPA_TH_SHVA_WATCH_SOCKET;
			/// rc = pepa_shva_thread_watch(core, my_name);
			rc = pepa_shva_thread_wait_fail(core, my_name);
			/* SHVA socket is invalid, we must reconnect */

			//if (-PEPA_ERR_THREAD_OUT_DOWN == pepa_test_fd(core->sockets.shva_rw)) {
			if (-PEPA_ERR_THREAD_SHVA_DOWN == rc) {
				next_step = PEPA_TH_SHVA_CLOSE_SOCKET;
				slog_info_l("SHVA is down");
			} else {
				/* Else OUT is down */
				slog_info_l("OUT is down");
				next_step = PEPA_TH_SHVA_WAIT_OUT;
			}

			//pthread_cancel(core->shva_forwarder.thread_id);
			pepa_thread_kill_shva_forwarder();

			slog_note_l("END STEP:   %s", pepa_shva_thread_state_str(next_step));
			//usleep(1000);
			break;

		case PEPA_TH_SHVA_CLOSE_SOCKET:
			slog_note_l("START STEP: %s", pepa_shva_thread_state_str(this_step));
			pepa_state_shva_set(core, PEPA_ST_FAIL);
			pepa_socket_close_shva_rw(core);
			next_step = PEPA_TH_SHVA_OPEN_CONNECTION;
			slog_note_l("END STEP:   %s", pepa_shva_thread_state_str(this_step));
			break;

		case PEPA_TH_SHVA_TERMINATE:
			slog_note_l("START STEP: %s", pepa_shva_thread_state_str(this_step));
			pepa_state_shva_set(core, PEPA_ST_FAIL);
			sleep(10);
			slog_note_l("END STEP:   %s", pepa_shva_thread_state_str(this_step));
			break;

		default:
			slog_fatal_l("Should never be here: next_steps = %d", next_step);
			abort();
			break;
		}
	} while (1);
	slog_fatal_l("Should never be here");
	abort();
	pthread_exit(NULL);
}

void pepa_shva_thread_new_forward_cleanup(__attribute__((unused))void *arg)
{
	int *eploll_fd = arg;
	close(*eploll_fd);
}

#define BUF_SIZE (2048)

void *pepa_shva_thread_new_forward(__attribute__((unused))void *arg)
{
	int                i;
	int                rc;
	pepa_core_t        *core            = pepa_get_core();
	char               *my_name         = "SHVA-FORWARD";
	char               buffer[BUF_SIZE];  //data buffer of 1K

	struct epoll_event events[2];
	int                epoll_fd         = epoll_create1(EPOLL_CLOEXEC);


	rc = pepa_pthread_init_phase(my_name);
	if (rc < 0) {
		slog_fatal_l("%s: Could not init the thread", my_name);
		//core->sockets.shva_rw = -1;
		close(epoll_fd);
		pthread_exit(NULL);
	}

	pthread_cleanup_push(pepa_shva_thread_new_forward_cleanup, epoll_fd);

	if (0 != epoll_ctl_add(epoll_fd, core->sockets.shva_rw, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
		//slog_warn_l("%s: Tried to add core->sockets.shva_rw = %d and failed", my_name,  core->sockets.shva_rw);
		close(epoll_fd);
		pthread_exit(NULL);
	}

	if (epoll_ctl_add(epoll_fd, core->sockets.out_write, EPOLLRDHUP | EPOLLHUP)) {
		//slog_warn_l("%s: Tried to add fdx->fd_write = %d and failed", my_name, core->sockets.out_write);
		close(epoll_fd);
		pepa_state_out_set(core, PEPA_ST_FAIL);
		pthread_exit(NULL);
	}

	slog_note_l("%s: socket in (SHVA): %d, socket out (OUT): %d", my_name, core->sockets.shva_rw, core->sockets.out_write);

	while (1)   {

		int event_count = epoll_wait(epoll_fd, events, 1, 300000);

		/* Interrupted by a signal */
		if (event_count < 0 && EINTR == errno) {
			continue;
		}

		if (event_count < 0) {
			slog_fatal_l("%s: error on wait: %s", my_name, strerror(errno));
			close(epoll_fd);
			pepa_state_out_set(core, PEPA_ST_FAIL);
			pthread_exit(NULL);
		}

		for (i = 0; i < event_count; i++) {
			/* If one of the read/write sockets is diconnected, exit the thread */
			if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
				close(epoll_fd);

				if (core->sockets.shva_rw == events[i].data.fd) {
					slog_warn_l("SHVA socket invalidated");
					close(epoll_fd);
					pepa_state_out_set(core, PEPA_ST_FAIL);
				}

				if (core->sockets.out_write == events[i].data.fd) {
					slog_warn_l("SHVA socket invalidated");
					close(epoll_fd);
					pepa_state_out_set(core, PEPA_ST_FAIL);
				}

				usleep(5000);
				pthread_exit(NULL);
			} /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) */

			/* Read from socket */
			if ((events[i].data.fd == core->sockets.shva_rw) && (events[i].events & EPOLLIN)) {
				rc = pepa_one_direction_copy2(/* Send to : */core->sockets.out_write, "OUT",
											  /* From: */core->sockets.shva_rw, "SHVA",
											  buffer, BUF_SIZE, /* Debug off */ 0,
											  /* RX stat */&core->monitor.shva_rx,
											  /* TX stat */&core->monitor.out_tx);

				if (PEPA_ERR_OK == rc) {
					// slog_note_l("%s: Sent to OUT OK", my_name);
					continue;
				}

				close(epoll_fd);

				if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
					slog_warn_l("%s: Could not write to OUT, closing read IN socket", my_name);
					close(epoll_fd);
					pepa_state_shva_set(core, PEPA_ST_FAIL);
				}

				if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
					slog_warn_l("%s: Could not write to OUT, closing read IN socket", my_name);
					close(epoll_fd);
					pepa_state_out_set(core, PEPA_ST_FAIL);
				}

				usleep(5000);
				pthread_exit(NULL);

			} /* End of read descriptor processing */
		}
	}
	pthread_cleanup_pop(0);
	pthread_exit(NULL);
}

#if 0 /* SEB */
void pepa_shva_thread_new_forward_cleanup(__attribute__((unused))void *arg){
	char        *my_name         = "SHVA-FORWARD-CLEAN";
	slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_warn_l("%s: Starting clean", my_name);
	slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");

}
	#define BUF_SIZE (2048)
void *pepa_shva_thread_new_forward(__attribute__((unused))void *arg){
	int         rc;
	pepa_core_t *core            = pepa_get_core();
	char        *my_name         = "SHVA-FORWARD";

	int         activity;
	int         max_sd           = 0;

	char        buffer[BUF_SIZE];  //data buffer of 1K

	//set of socket descriptors
	fd_set      readfds;
	pthread_cleanup_push(pepa_shva_thread_new_forward_cleanup, core);

	rc = pepa_pthread_init_phase(my_name);
	if (rc < 0) {
		slog_fatal_l("%s: Could not init the thread", my_name);
		core->sockets.shva_rw = -1;
		pepa_state_shva_set(core, PEPA_ST_FAIL);
		pthread_exit(NULL);
	}

	//accept the incoming connection

	int sock_in  = core->sockets.shva_rw;
	int sock_out = core->sockets.out_write;

	slog_note_l("%s: socket in (SHVA): %d, socket out (OUT): %d", my_name, sock_in, sock_out);

	while (1)   {
		//clear the socket set
		FD_ZERO(&readfds);

		/* Wait until daya on SHVA socket */
		FD_SET(sock_in, &readfds);
		max_sd = sock_in;

		activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

		/* Interrupted by a signal, continue */
		if ((activity < 0) && (errno == EINTR)) {
			//slog_note_l("%s: Select interrupted by a signal", my_name);
			continue;
		}

		if ((activity < 0) && (errno != EINTR)) {
			slog_fatal_l("%s: select error: %s", my_name, strerror(errno));
			pepa_socket_close_shva_rw(core);
			pepa_state_shva_set(core, PEPA_ST_FAIL);
			// pthread_exit(NULL);
			goto endit;
		}

		if (FD_ISSET(sock_in, &readfds)) {
			//slog_note_l("%s: Socket has something on it", my_name);
			rc = pepa_one_direction_copy2(/* Send to : */sock_out, "OUT",
										  /* From: */sock_in, "SHVA",
										  buffer, BUF_SIZE, /* Debug off */ 0,
										  /* RX stat */&core->monitor.shva_rx,
										  /* TX stat */&core->monitor.out_tx);
			//slog_note_l("%s: Finished copying to OUT", my_name);
			if (PEPA_ERR_OK == rc) {
				//slog_note_l("%s: Sent to OUT OK", my_name);
				continue;
			}

			if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
				slog_warn_l("%s: Could not read from SHVA", my_name);
				//close(core->sockets.shva_rw);
				//core->sockets.shva_rw = -1;
				pepa_state_shva_set(core, PEPA_ST_FAIL);
				//pthread_exit(NULL);
				goto endit;
			}

			if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
				slog_warn_l("%s: Could not write to OUT", my_name);
				//close(core->sockets.shva_rw);
				//core->sockets.shva_rw = -1;
				pepa_state_out_set(core, PEPA_ST_FAIL);
				//pthread_exit(NULL);
				goto endit;
			}

		}
	}
	endit:
	slog_warn_l("%s: Exiting thread", my_name);
	pthread_cleanup_pop(0);
	pthread_exit(NULL);
}
#endif

