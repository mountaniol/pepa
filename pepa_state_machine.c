#include <unistd.h>
#include <pthread.h>

#include "pepa_errors.h"
#include "pepa_core.h"
#include "buf_t/se_debug.h"

#if 0 /* SEB */
/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to FAIL state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int pepa_trans_to_state_fail(void){
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transit to disconnected state
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int pepa_trans_to_state_disconnected(void){
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to CONNECTING state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int pepa_trans_to_state_connecting(void){
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to ESTABLISHED state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int pepa_trans_to_state_established(void){
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to OPERATING state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int pepa_trans_to_state_operating(void){
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to COLLAPSE state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int pepa_trans_to_state_collapse(void){
	return PEPA_ERR_OK;
}

int pepa_change_state(pepa_state_t new_state){
	switch (new_state) {
		case PEPA_ST_FAIL:
		return pepa_trans_to_state_fail();
		case PEPA_ST_DISCONNECTED:
		return pepa_trans_to_state_disconnected();
		case PEPA_ST_CONNECTING:
		return pepa_trans_to_state_connecting();
		case PEPA_ST_ESTABLISHED:
		return pepa_trans_to_state_established();
		case PEPA_ST_OPERATING:
		return pepa_trans_to_state_operating();
		case PEPA_ST_COLLAPSE:
		return pepa_trans_to_state_collapse();
	}

	/* Shold never be here */
	return -1;
}

void *pepa_state_machine_thread(void *arg){
	int rc;
	pepa_core_t *core = pepa_get_core();
	while (1) {

		switch (core->state) {
			case PEPA_ST_FAIL:
			rc = pepa_change_state(PEPA_ST_DISCONNECTED);
			case PEPA_ST_DISCONNECTED:
			rc =  pepa_change_state(PEPA_ST_CONNECTING);
			case PEPA_ST_CONNECTING:
			rc = pepa_change_state(PEPA_ST_CONNECTING);
			case PEPA_ST_ESTABLISHED:
			return pepa_trans_to_state_established();
			case PEPA_ST_OPERATING:
			return pepa_trans_to_state_operating();
			case PEPA_ST_COLLAPSE:
			return pepa_trans_to_state_collapse();
		}
	} /* while (1) */
}
#endif

static void pepa_cancel_thread(pthread_t pid, const char *name)
{
	int rc = pthread_cancel(pid);
	if (0 != rc) {
		DE("Can not cancel %s thread\n", name);
	} else {
		DDD("Terminated %s thread\n", name);
	}
}
static void pepa_close_socket(int fd, const char *socket_name)
{
	if (fd > 0) {
		int rc = close(fd);
		if (0 != rc) {
			DE("Can not close %s, error %d\n",
			   socket_name, rc);
		}
	}
}

void pepa_back_to_disconnected_state(void)
{
	int                  rc;
	int                  index;
	pepa_core_t          *core = pepa_get_core();
	pepa_in_thread_fds_t *fds  = core->acceptor_shared;

	DD("BACK TO DISCONNECTED STATE\n");
	pepa_core_lock();
	sem_wait(&fds->buf_fds_mutex);

	/*** Terminate threads ***/

	pepa_cancel_thread(core->in_thread.thread_id, "IN");
	core->in_thread.thread_id = -1;

	pepa_cancel_thread(core->acceptor_thread.thread_id, "ACCEPTOR");
	core->acceptor_thread.thread_id = -1;

	pepa_cancel_thread(core->out_thread.thread_id, "OUT");
	core->out_thread.thread_id = -1;

	/*** Close all Acceptor file descriptors ***/

	if (NULL != fds->buf_fds) {
		for (index = 0; index < buf_arr_get_members_count(fds->buf_fds); index++) {
			const int *fd = buf_arr_get_member_ptr(fds->buf_fds, index);
			rc = close(*fd);
			if (0 != rc) {
				DE("Can not close Acceptor read socket fd (%d), error: %d\n", *fd, rc);
			}
		}

		rc = buf_free(fds->buf_fds);
		if (0 != rc) {
			DE("Can not free buf_array, memory leak is possible: %s\n",
			   buf_error_code_to_string(rc));
		}
		fds->buf_fds = NULL;
	}

	/* We do not need the semaphore anymore */
	sem_post(&fds->buf_fds_mutex);

	/** Close all IN file descriptors ***/

	if (NULL != core->buf_in_fds) {
		for (index = 0; index < buf_arr_get_members_count(core->buf_in_fds); index++) {
			const int *fd = buf_arr_get_member_ptr(core->buf_in_fds, index);
			rc = close(*fd);
			if (0 != rc) {
				DE("Can not close IN read socket fd (%d), error: %d\n", *fd, rc);
			}
		}

		rc = buf_free(core->buf_in_fds);
		if (0 != rc) {
			DE("Can not free buf_array, memory leak possible: %s\n",
			   buf_error_code_to_string(rc));
		}
		core->buf_in_fds = NULL;
	}

	/*** Close the rest of the sockets ****/

	/* Close the IN socket */
	pepa_close_socket(core->sockets.in_listen, "core->in_thread.fd_listen");
	core->sockets.in_listen = -1;

	/* Close the SHVA write socket */
	pepa_close_socket(core->sockets.shva_rw, "core->shva_thread.fd_write");
	core->sockets.shva_rw = -1;

	/* Close the OUT listen socket */
	pepa_close_socket(core->sockets.out_listen, "core->shva_thread.fd_write");
	core->sockets.out_listen = -1;

	/* Close the OUT listen socket */
	pepa_close_socket(core->sockets.out_read, "core->shva_thread.fd_write");
	core->sockets.out_read = -1;

	pepa_core_unlock();
}


