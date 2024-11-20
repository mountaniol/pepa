#include <unistd.h>

#include "slog/src/slog.h"
#include "pepa_core.h"
#include "pepa_config.h"
#include "pepa_errors.h"
#include "pepa_debug.h"
#include "pepa_socket_common.h"
#include "pepa_in_reading_sockets.h"

pepa_core_t *g_pepa_core = NULL;

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief Create (allocate)  'pepa_core_t' struct and feel it
 *  	  with defailt values, like '-1' for file descriptors
 * @param  void  
 * @return pepa_core_t* Allocated and inited pepa_core_t struct
 * @details The semaphor is inited and ready for usage when the
 *  		structure returned to caller 
 */
static pepa_core_t *pepa_create_core_t(void)
{
	pepa_core_t *core = calloc(sizeof(pepa_core_t), 1);
	TESTP_ASSERT(core, "Can not create core");

	/*** Init threads pthread descriptors */

	core->config = NULL;
	core->sockets.shva_listen = FD_CLOSED;
	core->sockets.shva_rw = FD_CLOSED;
	core->sockets.out_listen = FD_CLOSED;
	core->sockets.out_write = FD_CLOSED;
	core->sockets.in_listen = FD_CLOSED;

	core->shva_thread.thread_id = PTHREAD_DEAD;
	core->in_thread.thread_id = PTHREAD_DEAD;
	core->out_thread.thread_id = PTHREAD_DEAD;
	core->monitor_thread.thread_id = PTHREAD_DEAD;

	/*** Init initial buffer size  ***/
	core->internal_buf_size = COPY_BUF_SIZE_BYTES;
	core->print_buf_len = PRINT_BUF_SIZE_BYTES;

	/*** Init embedded values of number of allowed sockets ***/

	core->shva_thread.clients = PEPA_SHVA_SOCKETS;
	core->out_thread.clients = PEPA_OUT_SOCKETS;
	core->in_thread.clients = PEPA_IN_SOCKETS;

	// core->sockets.out_read = -1;
	core->monitor_timeout = MONITOR_TIMEOUT_USEC;
	core->slog_flags = 0;
	core->slog_file = NULL;
	core->slog_dir = NULL;
	core->slog_print = YES;
	core->monitor_divider = 1;
	core->emu_max_buf = 1024;
	core->emu_min_buf = 1;
	core->emu_in_threads = 1;
	core->monitor_freq = 5;
	core->pid_file_name = strdup("/tmp/pepa.pid");

	core->epoll_fd = FD_CLOSED;
	core->pid_fd = FD_CLOSED;
	core->validity = CORE_VALIDITY_MASK;

	core->readers_preopen = -1;
	core->use_id = NO;
	return core;
}

static int pepa_release_core_t(pepa_core_t *core)
{
	TESTP(core, -1);
	if (NO == pepa_core_is_valid(core)) {
		slog_fatal("Core is invalid, can not release it");
		return -1;
	}

	if (NULL != core->in_thread.ip_string) {
		int rc = buf_free(core->in_thread.ip_string);
		if (BUFT_OK != rc) {
			slog_note_l("Can not free the buf_t: %s", buf_error_code_to_string(rc));
		}
		core->in_thread.ip_string = NULL;
	}

	if (NULL != core->out_thread.ip_string) {
		int rc = buf_free(core->out_thread.ip_string);
		if (BUFT_OK != rc) {
			slog_note_l("Can not free the buf_t: %s", buf_error_code_to_string(rc));
		}
		core->out_thread.ip_string = NULL;
	}

	if (NULL != core->shva_thread.ip_string) {
		int rc = buf_free(core->shva_thread.ip_string);
		if (BUFT_OK != rc) {
			slog_note_l("Can not free the buf_t: %s", buf_error_code_to_string(rc));
		}
		core->shva_thread.ip_string = NULL;
	}

	if (NULL != core->print_buf) {
		free(core->print_buf);
		core->print_buf = NULL;
	}

	if (NULL != core->slog_file) {
		free(core->slog_file);
	}

	if (NULL != core->slog_dir) {
		free(core->slog_dir);
		core->slog_dir = NULL;
	}

	if (core->in_reading_sockets.number > 0) {
		pepa_in_reading_sockets_free(core);
	}

	if (core->buffer) {
		free(core->buffer);
		core->buffer = NULL;
	}

	if (FD_CLOSED != core->epoll_fd) {
		close(core->epoll_fd);
		core->epoll_fd = FD_CLOSED;
	}

	if (NULL != core->pid_file_name) {
		free(core->pid_file_name);
		core->pid_file_name = NULL;
	}

	free(core);
	g_pepa_core = NULL;

	return 0;
}

pepa_bool_t pepa_core_is_valid(const pepa_core_t *core)
{
	if (core->validity == CORE_VALIDITY_MASK) {
		return YES;
	}
	return NO;
}

/****** API FUNCTIONS *******/

int32_t pepa_core_init(void)
{
	g_pepa_core = pepa_create_core_t();
	if (NULL == g_pepa_core) {
		slog_fatal_l("Can not create core");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_CORE_CREATE);
	}
	return PEPA_ERR_OK;
}

int pepa_core_release(pepa_core_t *core)
{
	return pepa_release_core_t(core);
}

__attribute__((hot))
pepa_core_t *pepa_get_core(void)
{
	TESTP_ASSERT(g_pepa_core, "Core is NULL!");
	return g_pepa_core;
}

pepa_bool_t pepa_if_abort(void)
{
	if (NULL == g_pepa_core) {
		return PEPA_ERR_OK;
	}

	return g_pepa_core->abort_flag;
}


