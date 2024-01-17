#include <unistd.h>

#include "slog/src/slog.h"
#include "pepa_core.h"
#include "pepa_config.h"
#include "pepa_errors.h"
#include "pepa_debug.h"
#include "pepa_socket_common.h"

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

	core->sockets.shva_rw = FD_CLOSED;
	core->sockets.out_listen = FD_CLOSED;
	core->sockets.out_write = FD_CLOSED;
	core->sockets.in_listen = FD_CLOSED;

	core->shva_thread.thread_id = PTHREAD_DEAD;
	core->in_thread.thread_id = PTHREAD_DEAD;
	core->out_thread.thread_id = PTHREAD_DEAD;
	core->monitor_thread.thread_id = PTHREAD_DEAD;

	/*** Init initial buffer size  ***/
	core->internal_buf_size = COPY_BUF_SIZE_KB;

	/*** Init embedded values of number of allowed sockets ***/

	core->shva_thread.clients = PEPA_SHVA_SOCKETS;
	core->out_thread.clients = PEPA_OUT_SOCKETS;
	core->in_thread.clients = PEPA_IN_SOCKETS;

	core->sockets.shva_rw = -1;
	core->sockets.out_listen = -1;
	// core->sockets.out_read = -1;
	core->sockets.in_listen = -1;
	core->monitor_timeout = MONITOR_TIMEOUT_USEC;
	core->slog_flags = 0;
	core->slog_file = NULL;
	core->slog_dir = NULL;
	core->slog_print = 1;
	core->monitor_divider = 1;
	core->emu_max_buf = 1024;
	core->emu_min_buf = 1;
	core->emu_in_threads = 1;
	core->monitor_freq = 5;
	core->pid_file_name = strdup("/tmp/pepa.pid");

	core->epoll_fd = FD_CLOSED;
	core->pid_fd = FD_CLOSED;

	return core;
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

__attribute__((hot))
pepa_core_t *pepa_get_core(void)
{
	TESTP_ASSERT(g_pepa_core, "Core is NULL!");
	return g_pepa_core;
}

int32_t pepa_if_abort(void)
{
	if (NULL == g_pepa_core) {
		return PEPA_ERR_OK;
	}

	return g_pepa_core->abort_flag;
}


