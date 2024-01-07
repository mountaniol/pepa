#if 0 /* SEB */
	#define _GNU_SOURCE
	#include <unistd.h>
	#include <errno.h>

	#include "slog/src/slog.h"
	#include "pepa_socket_common.h"
	#include "pepa_errors.h"
	#include "pepa_state_machine.h"

static int32_t pepa_out_wait_connection(pepa_core_t *core, int32_t fd_listen){
	struct sockaddr_in s_addr;
	socklen_t          addrlen  = sizeof(struct sockaddr);
	const char         *my_name = "OUT-ACCEPT";
	int32_t                fd_read  = -1;
	do {
		slog_info_l("%s: Starting accept() waiting", my_name);

		fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC);

	} while (fd_read < 0);

	slog_info_l("%s: ACCEPTED CONNECTION: fd = %d", my_name, fd_read);
	//pepa_set_tcp_connection_props(core, fd_read);
	pepa_set_tcp_timeout(fd_read);
	pepa_set_tcp_send_size(core, fd_read);
	return fd_read;
}

static int32_t pepa_out_thread_start(char *name){
	return pepa_pthread_init_phase(name);
}

static int32_t pepa_out_thread_open_listening_socket(pepa_core_t *core, __attribute__((unused)) char *my_name){
	struct sockaddr_in s_addr;
	int32_t                waiting_time = 0;
	int32_t                timeout      = 5;

	if (core->sockets.out_listen >= 0) {
		slog_note_l("Trying to open a listening socket while it is already opened");
	}
	do {
		/* Just try to close it */
		core->sockets.out_listen = pepa_open_listening_socket(&s_addr,
															  core->out_thread.ip_string,
															  core->out_thread.port_int,
															  core->out_thread.clients,
															  __func__);
		if (core->sockets.out_listen < 0) {
			core->sockets.out_listen = -1;
			//slog_warn_l("%s: Can not open listening socket: %s", my_name, strerror(errno));
			waiting_time += timeout;
			//usleep(1000000);
		}
	} while (core->sockets.out_listen < 0);
	usleep(1000);
	return PEPA_ERR_OK;
}

static int32_t pepa_out_thread_accept(pepa_core_t *core, __attribute__((unused))char *my_name){
	int32_t fd_read = pepa_out_wait_connection(core, core->sockets.out_listen);
	core->sockets.out_write = fd_read;
	return PEPA_ERR_OK;
}

static int32_t pepa_out_thread_close_listen(pepa_core_t *core, __attribute__((unused))char *my_name){
	pepa_socket_close_out_write(core);
	pepa_socket_close_out_listen(core);
	slog_info("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_info("$$$$$$$    CLOSED <OUT> LISTEN SOCK      $$$$$$$$$");
	slog_info("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	return PEPA_ERR_OK;
}

/* Wait for signal; when SHVA is DOWN, return */
static int32_t pepa_out_thread_wait_fail(pepa_core_t *core, __attribute__((unused)) const char *my_name){
	while (1) {
		if (PEPA_ST_FAIL == pepa_state_out_get(core)) {
			slog_note_l("%s: OUT became FAIL", my_name);
			return -PEPA_ERR_THREAD_OUT_FAIL;
		}

		if (PEPA_ST_FAIL == pepa_state_shva_get(core)) {
			slog_note_l("%s: SHVA became FAIL", my_name);
			return -PEPA_ERR_THREAD_SHVA_FAIL;
		}

		/* We exit this wait only when SHVA is ready */
		pepa_state_wait(core);
		slog_note_l("GOT SOME SIGNAL");
	};
	return PEPA_ERR_OK;
}

void *pepa_out_thread(__attribute__((unused))void *arg){
	pepa_out_thread_state_t next_step = PEPA_TH_OUT_START;
	char                    *my_name  = "OUT";
	int32_t                     rc;
	pepa_core_t             *core     = pepa_get_core();

	set_sig_handler();

	do {
		pepa_out_thread_state_t this_step = next_step;
		switch (next_step) {
			case 	PEPA_TH_OUT_START:
			slog_note_l("START STEP: %s", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_CREATE_LISTEN;
			rc = pepa_out_thread_start(my_name);
			if (rc < 0) {
				slog_fatal_l("%s: Could not init the thread", my_name);
				pepa_state_out_set(core, PEPA_ST_DOWN);
				next_step = PEPA_TH_OUT_TERMINATE;
			}
			slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

			case PEPA_TH_OUT_CREATE_LISTEN:
			slog_note_l("START STEP: %s", pepa_out_thread_state_str(this_step));
			next_step = PEPA_TH_OUT_ACCEPT;
			rc = pepa_out_thread_open_listening_socket(core, my_name);
			if (rc) {
				slog_warn_l("%s: Can not open listening socket", my_name);
				sleep(1);
				next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
			}
			slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

			case PEPA_TH_OUT_ACCEPT:
			slog_note_l("START STEP: %s", pepa_out_thread_state_str(this_step));

			next_step = PEPA_TH_OUT_WATCH_WRITE_SOCK;

			if (0 != pepa_test_fd(core->sockets.out_listen)) {
				slog_warn_l("%s: Can not start accept: listening socket is invalid: fd %d", my_name, core->sockets.out_listen);
				next_step	= PEPA_TH_OUT_CLOSE_LISTEN_SOCKET;
				slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
				break;
			}

			rc = pepa_out_thread_accept(core, my_name);
			if (rc) {
				slog_warn_l("%s: Can not accept incoming connection", my_name);
				next_step = PEPA_TH_OUT_CLOSE_WRITE_SOCKET;
				slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
				break;
			}

			pepa_thread_start_shva(core);
			pepa_state_out_set(core, PEPA_ST_RUN);

			slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

			case PEPA_TH_OUT_WATCH_WRITE_SOCK:
			slog_note_l("START STEP: %s", pepa_out_thread_state_str(this_step));
			/* TODO */
			rc = pepa_out_thread_wait_fail(core, my_name);
			next_step = PEPA_TH_OUT_CLOSE_LISTEN_SOCKET;
			slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

			case PEPA_TH_OUT_CLOSE_WRITE_SOCKET:
			slog_note_l("START STEP: %s", pepa_out_thread_state_str(this_step));
			pepa_socket_close_out_write(core);
			next_step = PEPA_TH_OUT_ACCEPT;
			slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

			case PEPA_TH_OUT_CLOSE_LISTEN_SOCKET:
			slog_note_l("START STEP: %s", pepa_out_thread_state_str(this_step));

			/* When the listen socket is closed, is also closes the write socket */
			pepa_thread_kill_shva(core);
			rc = pepa_out_thread_close_listen(core, my_name);
			pepa_state_out_set(core, PEPA_ST_FAIL);

			next_step = PEPA_TH_OUT_CREATE_LISTEN;
			slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
			break;

			case PEPA_TH_OUT_TERMINATE:
			slog_note_l("START STEP: %s", pepa_out_thread_state_str(this_step));
			pepa_state_out_set(core, PEPA_ST_FAIL);
			pepa_state_out_set(core, PEPA_ST_DOWN);
			sleep(10);
			slog_note_l("END STEP  : %s", pepa_out_thread_state_str(this_step));
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
#endif

