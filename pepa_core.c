#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h> /* For eventfd */

#include "pepa_core.h"
#include "pepa_config.h"
#include "pepa_errors.h"
#include "pepa_debug.h"
#include "buf_t/se_debug.h"

static pepa_core_t *g_pepa_core = NULL;

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief Init the semaphore (->mutex) of the pepa_core_t
 *  	  structure
 * @param pepa_core_t* core  An instance of pepa_core_t
 *  				 structure to init semaphore
 * @return int 0 on success, -1 on an error
 * @details You need to use this function if you clean an
 *  		existing core structure, and want to reuse it. After
 *  		cleaning the structure, the semaphore is destroyes
 *  		and not reinited
 */
static int pepa_core_sem_init(pepa_core_t *core)
{
	TESTP(core, -1);
	int sem_rc = sem_init(&core->mutex, 0, 1);

	if (0 != sem_rc) {
		DE("Could not init mutex\n");
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
 * @return int 0 on success, -1 on an error
 * @details YOu should never use this function; however, in the
 *  		future might be some change in the code and this
 *  		function should be used.
 */
static int pepa_core_sem_destroy(pepa_core_t *core)
{
	TESTP(core, -1);
	const int sem_rc = sem_destroy(&core->mutex);
	if (0 != sem_rc) {
		DE("Could not destroy mutex\n");
		perror("sem destroy failure: ");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_DESTROY_MITEX);
	}
	return PEPA_ERR_OK;
}

static void pepa_core_set_default_values(pepa_core_t *core)
{
	TESTP_VOID(core);
	core->shva_thread.fd_listen = -1;
	core->in_thread.fd_listen = -1;
	core->out_thread.fd_listen = -1;
}

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief Create (allocate)  'pepa_core_t' struct and feel it
 *  	  with defailt values, like '-1' for file descriptors
 * @param  void  
 * @return pepa_core_t* Allocated and inited pepa_core_t struct
 * @details THe semaphor is inited and ready for usage when the
 *  		structure returned to caller 
 */
static pepa_core_t *pepa_create_core_t(void)
{
	int         rc;

	pepa_core_t *core = malloc(sizeof(pepa_core_t));
	TESTP_ASSERT(core, "Can not create core");
	pepa_core_set_default_values(core);
	rc = pepa_core_sem_init(core);
	if (rc) {
		DE("Could not init mutex - returning NULL\n");
		free(core);
		PEPA_TRY_ABORT();
		return NULL;
	}

	core->internal_buf_size = COPY_BUF_SIZE;

	/* Create event fd control descriptors */
	core->controls.ctl_from_acceptor = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	core->controls.ctl_from_shva = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	core->controls.ctl_from_in = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	core->controls.ctl_from_acceptor = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	core->controls.shva_from_ctl = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	return core;
}

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief Clean the pepa_core_t structure, and reset all values
 *  	  to default state; WARNING: semaphor (->mutex) is
 *  	  destroyes and NOT inited again
 * @param pepa_core_t* core  
 * @return int 0 on success, -1 on an error
 * @details 
 */
static int pepa_clean_core_t(pepa_core_t *core)
{
	TESTP(core, -PEPA_ERR_NULL_POINTER);

	/* Release mutex */
	const int sem_rc = pepa_core_sem_destroy(core);
	if (0 != sem_rc) {
		DE("Could not destroy mutex\n");
		PEPA_TRY_ABORT();
		return (sem_rc);
	}
	pepa_core_set_default_values(core);
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief Completely deinit and deallocate the pepa_core_t
 *  	  struct. THe file na,e string is freed, if allocated;
 *  	  the semaphor is destroyed.
 * @param pepa_core_t* core  The instance of the structure to be
 *  				 destroyed
 * @return int 0 on success, -1 on en error
 * @details WARNING: The semaphore (->mutex) must be posted when
 *  		this function is called; else the behavoir is
 *  		undefined. For more, see 'man sem_destry'
 */
static int pepa_destroy_core_t(pepa_core_t *core)
{
	int       rc;
	const int clean_rc = pepa_clean_core_t(core);
	if (0 != clean_rc) {
		DE("Could not clean core - return error\n");
		return clean_rc;
	}

	if (core->shva_thread.ip_string) {
		rc = buf_free(core->shva_thread.ip_string);
		if (BUFT_OK != rc) {
			DE("Could not free buf_t\n");
			PEPA_TRY_ABORT();
		}
	}

	if (core->shva_thread.fd_listen > 0) {
		close(core->shva_thread.fd_listen);
	}

	if (core->in_thread.ip_string) {
		rc = buf_free(core->in_thread.ip_string);
		if (BUFT_OK != rc) {
			DE("Could not free buf_t\n");
			PEPA_TRY_ABORT();
		}
	}

	if (core->in_thread.fd_listen > 0) {
		close(core->in_thread.fd_listen);
	}

	if (core->out_thread.ip_string) {
		rc = buf_free(core->out_thread.ip_string);
		if (BUFT_OK != rc) {
			DE("Could not free buf_t\n");
			PEPA_TRY_ABORT();
		}
	}

	if (core->out_thread.fd_listen > 0) {
		close(core->out_thread.fd_listen);
	}

	/* Clean the core before release it, secure reasons */
	memset(core, 0, sizeof(pepa_core_t));
	free(core);
	return PEPA_ERR_OK;
}

/****** API FUNCTIONS *******/

int pepa_core_init(void)
{
	g_pepa_core = pepa_create_core_t();
	if (NULL == g_pepa_core) {
		DE("Can not create core\n");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_CORE_CREATE);
	}
	return PEPA_ERR_OK;
}

int pepa_core_finish(void)
{
	if (NULL == g_pepa_core) {
		DE("Can not destroy core, it is NULL already!\n");
		PEPA_TRY_ABORT();
		return (-PEPA_ERR_CORE_DESTROY);
	}

	return pepa_destroy_core_t(g_pepa_core);
}

pepa_core_t *pepa_get_core(void)
{
	TESTP_ASSERT(g_pepa_core, "Core is NULL!");
	return g_pepa_core;
}

int pepa_core_lock(void)
{
	int rc;
	TESTP_ASSERT(g_pepa_core, "Core is NULL!");
	sem_getvalue(&g_pepa_core->mutex, &rc);
	if (rc > 1) {
		DE("Semaphor count is too high: %d > 1\n", rc);
		PEPA_TRY_ABORT();
		abort();
	}

	DD("Gettin sem\n\r");
	rc = sem_wait(&g_pepa_core->mutex);
	if (0 != rc) {
		DE("Can't wait on semaphore; abort\n");
		perror("Can't wait on semaphore; abort");
		PEPA_TRY_ABORT();
		abort();
	}

	return PEPA_ERR_OK;
}

int pepa_core_unlock(void)
{
	int rc;
	TESTP_ASSERT(g_pepa_core, "Core is NULL!");
	sem_getvalue(&g_pepa_core->mutex, &rc);
	if (rc > 0) {
		DE("Tried to unlock not locked left semaphor\n\r");
		PEPA_TRY_ABORT();
		abort();
	}

	DD("Putting sem\n\r");
	rc = sem_post(&g_pepa_core->mutex);
	if (0 != rc) {
		DE("Can't unlock semaphore: abort\n");
		perror("Can't unlock semaphore: abort");
		PEPA_TRY_ABORT();
		abort();
	}

	return PEPA_ERR_OK;
}


int pepa_if_abort()
{
	if (NULL == g_pepa_core) {
		return 0;
	}

	return g_pepa_core->abort_flag;
}

