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
 * @brief Init the semaphore (->mutex) of the pepa_core_t
 *  	  structure
 * @param pepa_core_t* core  An instance of pepa_core_t
 *  				 structure to init semaphore
 * @return int32_t 0 on success, -1 on an error
 * @details You need to use this function if you clean an
 *  		existing core structure, and want to reuse it. After
 *  		cleaning the structure, the semaphore is destroyes
 *  		and not reinited
 */
static int32_t pepa_core_sem_init(pepa_core_t *core)
{
	TESTP(core, -1);
	int32_t sem_rc = sem_init(&core->mutex, 0, 1);

	if (0 != sem_rc) {
		slog_fatal_l("Could not init mutexes");
		perror("sem init failure: ");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_INIT_MITEX);
	}

	return PEPA_ERR_OK;
}

static int32_t pepa_shva_mutex_init(pepa_core_t *core)
{
	TESTP(core, -1);
	int32_t sem_rc = sem_init(&core->sockets.shva_rw_mutex, 0, 1);

	if (0 != sem_rc) {
		slog_fatal_l("Could not init SHVA mutexes");
		perror("sem init failure: ");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_INIT_MITEX);
	}

	return PEPA_ERR_OK;
}

static void pepa_core_set_default_values(pepa_core_t *core)
{
	TESTP_VOID(core);
	// core->shva_thread.fd_listen = -1;
	core->sockets.shva_rw = -1;
	core->sockets.out_listen = -1;
	core->sockets.out_write = -1;
	core->sockets.in_listen = -1;
}

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
	int32_t         rc;

	pepa_core_t *core = calloc(sizeof(pepa_core_t), 1);
	TESTP_ASSERT(core, "Can not create core");
	pepa_core_set_default_values(core);
	rc = pepa_core_sem_init(core);
	if (rc) {
		slog_fatal_l("Could not init mutex - returning NULL");
		free(core);
		PEPA_TRY_ABORT();
		return NULL;
	}

	rc = pepa_shva_mutex_init(core);
	if (rc) {
		slog_fatal_l("Could not init SHAV mutex - returning NULL");
		free(core);
		PEPA_TRY_ABORT();
		return NULL;
	}

	/*** Init threads pthread descriptors */
	
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
	core->monitor_freq = 5;
	core->pid_file_name = strdup("/tmp/pepa.pid");

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

int32_t pepa_core_lock(void)
{
	int32_t rc;
	TESTP_ASSERT(g_pepa_core, "Core is NULL!");
	sem_getvalue(&g_pepa_core->mutex, &rc);
	if (rc > 1) {
		slog_fatal_l("Semaphor count is too high: %d > 1", rc);
		PEPA_TRY_ABORT();
		abort();
	}

	rc = sem_wait(&g_pepa_core->mutex);
	if (0 != rc) {
		slog_fatal_l("Can't wait on semaphore; abort");
		perror("Can't wait on semaphore; abort");
		PEPA_TRY_ABORT();
		abort();
	}

	return PEPA_ERR_OK;
}


int32_t pepa_if_abort(void)
{
	if (NULL == g_pepa_core) {
		return PEPA_ERR_OK;
	}

	return g_pepa_core->abort_flag;
}


