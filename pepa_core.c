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

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief Destroy the pepa_core_t internal semaphore
 * @param pepa_core_t* core  Instance of pepa_core_t structure
 *  				 to destroy semaphore
 * @return int32_t 0 on success, -1 on an error
 * @details YOu should never use this function; however, in the
 *  		future might be some change in the code and this
 *  		function should be used.
 */
#if 0 /* SEB */
static int32_t pepa_core_sem_destroy(pepa_core_t *core){
	TESTP(core, -1);
	const int32_t sem_rc = sem_destroy(&core->mutex);
	if (0 != sem_rc) {
		slog_fatal_l("Could not destroy mutex");
		perror("sem destroy failure: ");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_DESTROY_MITEX);
	}
	return PEPA_ERR_OK;
}
#endif

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

	/*** Init initial values of state machibe  */

	//core->state.signals[PEPA_PR_SHVA] = PEPA_ST_DOWN;
	//core->state.signals[PEPA_PR_IN] = PEPA_ST_DOWN;
	//core->state.signals[PEPA_PR_OUT] = PEPA_ST_DOWN;

	/*** Init threads pthread descriptors */
	
	core->shva_thread.thread_id = PTHREAD_DEAD;
	// core->shva_forwarder.thread_id = PTHREAD_DEAD;
	core->in_thread.thread_id = PTHREAD_DEAD;
	// core->in_forwarder.thread_id = PTHREAD_DEAD;
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

#if 0 /* SEB */
	rc = pthread_mutex_init(&core->state.sync_sem, NULL);
	if (rc < 0) {
		slog_fatal_l("Can not init core->state.sync_sem");
		abort();
	}

	rc = pthread_mutex_init(&core->state.signals_sem, NULL);
	if (rc < 0) {
		slog_fatal_l("Can not init core->state.sync_sem");
		abort();
	}

	rc = pthread_cond_init(&core->state.sync, NULL);
	if (rc < 0) {
		slog_fatal_l("Can not init core->state.sync");
		abort();
	}
#endif	

	core->pid_file_name = strdup("/tmp/pepa.pid");

	return core;
}

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief Clean the pepa_core_t structure, and reset all values
 *  	  to default state; WARNING: semaphor (->mutex) is
 *  	  destroyes and NOT inited again
 * @param pepa_core_t* core  
 * @return int32_t 0 on success, -1 on an error
 * @details 
 */
#if 0 /* SEB */
static int32_t pepa_clean_core_t(pepa_core_t *core){
	TESTP(core, -PEPA_ERR_NULL_POINTER);

	/* Release mutex */
	const int sem_rc = pepa_core_sem_destroy(core);
	if (0 != sem_rc) {
		slog_fatal_l("Could not destroy mutex");
		PEPA_TRY_ABORT();
		return (sem_rc);
	}
	pepa_core_set_default_values(core);
	return PEPA_ERR_OK;
}
#endif

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief Completely deinit and deallocate the pepa_core_t
 *  	  struct. THe file na,e string is freed, if allocated;
 *  	  the semaphor is destroyed.
 * @param pepa_core_t* core  The instance of the structure to be
 *  				 destroyed
 * @return int32_t 0 on success, -1 on en error
 * @details WARNING: The semaphore (->mutex) must be posted when
 *  		this function is called; else the behavoir is
 *  		undefined. For more, see 'man sem_destry'
 */
#if 0 /* SEB */
static int32_t pepa_destroy_core_t(pepa_core_t *core){
	int32_t       rc;
	const int32_t clean_rc = pepa_clean_core_t(core);
	if (0 != clean_rc) {
		slog_fatal_l("Could not clean core - return error");
		return clean_rc;
	}

	if (core->shva_thread.ip_string) {
		rc = buf_free(core->shva_thread.ip_string);
		if (BUFT_OK != rc) {
			slog_fatal_l("Could not free buf_t");
			PEPA_TRY_ABORT();
		}
	}

	if (core->in_thread.ip_string) {
		rc = buf_free(core->in_thread.ip_string);
		if (BUFT_OK != rc) {
			slog_fatal_l("Could not free buf_t");
			PEPA_TRY_ABORT();
		}
	}

	pepa_socket_close_in_listen(core);
	if (core->out_thread.ip_string) {
		rc = buf_free(core->out_thread.ip_string);
		if (BUFT_OK != rc) {
			slog_fatal_l("Could not free buf_t");
			PEPA_TRY_ABORT();
		}
	}
	pepa_socket_close_out_listen(core);
	/* Clean the core before release it, secure reasons */
	memset(core, 0, sizeof(pepa_core_t));
	free(core);
	return PEPA_ERR_OK;
}
#endif

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

#if 0 /* SEB */
int32_t pepa_core_finish(void){
	if (NULL == g_pepa_core) {
		slog_fatal_l("Can not destroy core, it is NULL already!");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_CORE_DESTROY);
	}

	return pepa_destroy_core_t(g_pepa_core);
}
#endif

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

#if 0 /* SEB */
int32_t pepa_core_unlock(void){
	int32_t rc;
	TESTP_ASSERT(g_pepa_core, "Core is NULL!");
	sem_getvalue(&g_pepa_core->mutex, &rc);
	if (rc > 0) {
		slog_fatal_l("Tried to unlock not locked left semaphor\r");
		PEPA_TRY_ABORT();
		abort();
	}

	rc = sem_post(&g_pepa_core->mutex);
	if (0 != rc) {
		slog_fatal_l("Can't unlock semaphore: abort");
		perror("Can't unlock semaphore: abort");
		PEPA_TRY_ABORT();
		abort();
	}

	return PEPA_ERR_OK;
}
#endif

#if 0 /* SEB */
void pepa_shva_socket_lock(pepa_core_t *core){
	int32_t rc;
	TESTP_ASSERT(g_pepa_core, "Core is NULL!");

	rc = sem_wait(&core->sockets.shva_rw_mutex);
	if (0 != rc) {
		slog_fatal_l("Can't wait on semaphore; abort");
		perror("Can't wait on semaphore; abort");
		abort();
	}
}
#endif

#if 0 /* SEB */
void pepa_shva_socket_unlock(pepa_core_t *core){
	int32_t rc;
	rc = sem_post(&core->sockets.shva_rw_mutex);
	if (0 != rc) {
		slog_fatal_l("Can't unlock semaphore: abort");
		perror("Can't unlock semaphore: abort");
		abort();
	}
}
#endif

int32_t pepa_if_abort(void)
{
	if (NULL == g_pepa_core) {
		return PEPA_ERR_OK;
	}

	return g_pepa_core->abort_flag;
}

#if 0 /* SEB */
const char *pepa_out_thread_state_str(pepa_out_thread_state_t s){
	switch (s) {
		case PEPA_TH_OUT_START:
		return ("PEPA_TH_OUT_START"); /* Start thread routines */
		case PEPA_TH_OUT_CREATE_LISTEN:
		return ("PEPA_TH_OUT_CREATE_LISTEN"); /* Create listening socket */
		case PEPA_TH_OUT_ACCEPT:
		return ("PEPA_TH_OUT_ACCEPT"); /* Run accept() which creates Write socket */
		case PEPA_TH_OUT_WATCH_WRITE_SOCK:
		return ("PEPA_TH_OUT_WATCH_WRITE_SOCK"); /* Watch the status of Write  socket */
		case PEPA_TH_OUT_CLOSE_WRITE_SOCKET:
		return ("PEPA_TH_OUT_CLOSE_WRITE_SOCKET"); /* Close Write socket */
		case PEPA_TH_OUT_CLOSE_LISTEN_SOCKET:
		return ("PEPA_TH_OUT_CLOSE_LISTEN_SOCKET"); /* Close listening socket */
		case PEPA_TH_OUT_TERMINATE:
		return ("PEPA_TH_OUT_TERMINATE"); /* Terminate thread */
	}
	return "UNKNOWN";
}
#endif

#if 0 /* SEB */
const char *pepa_shva_thread_state_str(pepa_shva_thread_state_t s){
	switch (s) {
		case PEPA_TH_SHVA_START:
		return ("PEPA_TH_SHVA_START"); /* Start thread routines */
		case PEPA_TH_SHVA_OPEN_CONNECTION:
		return ("PEPA_TH_SHVA_OPEN_CONNECTION"); /* Start thread routines */
		case PEPA_TH_SHVA_WAIT_OUT:
		return ("PEPA_TH_SHVA_WAIT_OUT");
		case PEPA_TH_SHVA_START_TRANSFER:
		return ("PEPA_TH_SHVA_START_TRANSFER"); /* Start transfering thread */
		case PEPA_TH_SHVA_WATCH_SOCKET:
		return ("PEPA_TH_SHVA_WATCH_SOCKET"); /* Watch the status of Write  socket */
		case PEPA_TH_SHVA_CLOSE_SOCKET:
		return ("PEPA_TH_SHVA_CLOSE_SOCKET"); /* Close Write socket */
		case PEPA_TH_SHVA_TERMINATE:
		return ("PEPA_TH_SHVA_TERMINATE"); /* Close listening socket */
	}
	return "UNKNOWN";
}
#endif

#if 0 /* SEB */
const char *pepa_in_thread_state_str(pepa_in_thread_state_t s){
	switch (s) {
		case PEPA_TH_IN_START:
		return ("PEPA_TH_IN_START"); /* Start thread routines */
		case PEPA_TH_IN_CREATE_LISTEN:
		return ("PEPA_TH_IN_CREATE_LISTEN"); /* Run accept() which creates Write socket */
		case PEPA_TH_IN_CLOSE_LISTEN:
		return ("PEPA_TH_IN_CLOSE_LISTEN"); /* Run accept() which creates Write socket */
		case PEPA_TH_IN_TEST_LISTEN_SOCKET:
		return ("PEPA_TH_IN_TEST_LISTEN_SOCKET"); /* Start transfering thread */
		case PEPA_TH_IN_WAIT_SHVA_UP:
		return ("PEPA_TH_IN_WAIT_SHVA_UP"); /* Watch the status of Write  socket */
		case PEPA_TH_IN_WAIT_SHVA_DOWN:
		return ("PEPA_TH_IN_WAIT_SHVA_DOWN"); /* Watch the status of Write  socket */
		case PEPA_TH_IN_START_TRANSFER:
		return ("PEPA_TH_IN_START_TRANSFER"); /* Close listening socket */
		case PEPA_TH_IN_TERMINATE:
		return ("PEPA_TH_IN_TERMINATE"); /* Close listening socket */
	}
	return "UNKNOWN";
}
#endif

