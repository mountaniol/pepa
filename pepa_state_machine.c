#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <pthread.h>
#include <semaphore.h>

#include "pepa_config.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_socket_common.h"
#include "pepa_socket_out.h"
#include "pepa_socket_shva.h"
#include "pepa_socket_in.h"
#include "buf_t/se_debug.h"

static void pepa_thread_cancel(pthread_t pid, const char *name)
{
	if (PTHREAD_DEAD == pid || pthread_kill(pid, 0) < 0) {
		DE("Can not cancel %s thread, it is not alive\n", name);
		return;
	}

	int rc = pthread_cancel(pid);
	if (0 != rc) {
		DE("Can not cancel %s thread\n", name);
	} else {
		DDD("Canceled %s thread\n", name);
	}
}

static void pepa_socket_close(int fd, const char *socket_name)
{
	if (fd > 0) {
		int rc = close(fd);
		if (0 != rc) {
			DE("Can not close %s, error %d\n",
			   socket_name, rc);
		}
	}
}

int pepa_thread_is_shva_up(void)
{
	pepa_core_t          *core = pepa_get_core();
	if (PTHREAD_DEAD == core->shva_thread.thread_id ||
		pthread_kill(core->shva_thread.thread_id, 0) < 0) {
		return -PEPA_ERR_THREAD_DEAD;
	}
	return PEPA_ERR_OK;
}

int pepa_thread_is_in_up(void)
{
	pepa_core_t          *core = pepa_get_core();

	DDD("core->in_thread.thread_id = %lX\n", core->in_thread.thread_id);
	if (PTHREAD_DEAD == core->in_thread.thread_id) {
		DE("THREAD IN IS DEAD: core->in_thread.thread_id = %lX\n", core->in_thread.thread_id);
		return -PEPA_ERR_THREAD_DEAD;
	}
	if (pthread_kill(core->in_thread.thread_id, 0) < 0) {
		DE("THREAD IN IS DEAD: kill = %d\n", pthread_kill(core->in_thread.thread_id, 0));
		return -PEPA_ERR_THREAD_DEAD;
	}
	return PEPA_ERR_OK;
}

int pepa_thread_is_out_up(void)
{
	pepa_core_t          *core = pepa_get_core();

	if (PTHREAD_DEAD == core->out_thread.thread_id ||
		pthread_kill(core->out_thread.thread_id, 0) < 0) {
		return -PEPA_ERR_THREAD_DEAD;
	}
	return PEPA_ERR_OK;
}

void pepa_thread_kill_shva(void)
{
	pepa_core_t          *core = pepa_get_core();
	pepa_thread_cancel(core->shva_thread.thread_id, "SHVA");
	core->shva_thread.thread_id = PTHREAD_DEAD;

	DD("#############################################\n");
	DD("##       THREAD SHVA IS KILLED             ##\n");
	DD("#############################################\n");
}

void pepa_thread_kill_out(void)
{
	pepa_core_t          *core = pepa_get_core();
	pepa_thread_cancel(core->out_thread.thread_id, "OUT");
	core->out_thread.thread_id = PTHREAD_DEAD;

	DD("#############################################\n");
	DD("##       THREAD OUT IS KILLED              ##\n");
	DD("#############################################\n");
}

void pepa_thread_kill_in(void)
{
	pepa_core_t          *core = pepa_get_core();
	pepa_thread_cancel(core->in_thread.thread_id, "IN");
	core->in_thread.thread_id = PTHREAD_DEAD;

	DD("#############################################\n");
	DD("##       THREAD IN IS KILLED                ##\n");
	DD("#############################################\n");
}

void pepa_thread_start_out(void)
{
	pepa_core_t          *core = pepa_get_core();
	DDD("Starting OUT thread\n");
	if (PEPA_ERR_OK == pepa_thread_is_out_up()) {
		DDD("Thread OUT is UP already, finishing");
		return;
	}
	int rc    = pthread_create(&core->out_thread.thread_id, NULL, pepa_out_thread, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}

	DD("#############################################\n");
	DD("##       THREAD OUT IS STARTED              ##\n");
	DD("#############################################\n");

}

void pepa_thread_start_shva(void)
{
	pepa_core_t          *core = pepa_get_core();
	DDD("Starting SHVA thread\n");
	if (PEPA_ERR_OK == pepa_thread_is_shva_up()) {
		DDD("Thread SHVA is UP already, finishing");
		return;
	}

	int rc    = pthread_create(&core->shva_thread.thread_id, NULL, pepa_shva_thread_new, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}

	DD("#############################################\n");
	DD("##       THREAD SHVA IS STARTED            ##\n");
	DD("#############################################\n");

}

void pepa_thread_start_in(void)
{
	pepa_core_t          *core = pepa_get_core();
	DDD("Starting IN thread\n");
	if (PEPA_ERR_OK == pepa_thread_is_in_up()) {
		DDD("Thread IN is UP already, finishing");
		return;
	}

	int rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_in_thread_new, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}

	DD("#############################################\n");
	DD("##       THREAD IN IS STARTED              ##\n");
	DD("#############################################\n");
}

void pepa_back_to_disconnected_state_new(void)
{
	static int  counter = 0;
	pepa_core_t *core   = pepa_get_core();

	DD("#############################################\n");
	DD("#############################################\n");
	DD("#############################################\n");
	DD("##       BACK TO DISCONNECTED STATE        ##\n");
	DD("#############################################\n");
	DD("#############################################\n");
	DD("#############################################\n");

	pepa_core_lock();

	/*** Terminate threads ***/

	pepa_thread_kill_out();
	pepa_thread_kill_in();
	pepa_thread_kill_shva();

	/*** Close the rest of the sockets ****/

	/* Close the IN socket */
	pepa_socket_close(core->sockets.in_listen, "core->in_thread.fd_listen");
	core->sockets.in_listen = -1;

	/* Close the SHVA write socket */
	pepa_socket_close(core->sockets.shva_rw, "core->shva_thread.fd_write");
	core->sockets.shva_rw = -1;

	/* Close the OUT listen socket */
	pepa_socket_close(core->sockets.out_listen, "core->shva_thread.fd_write");
	core->sockets.out_listen = -1;

	/* Close the OUT listen socket */
	pepa_socket_close(core->sockets.out_write, "core->shva_thread.fd_write");
	core->sockets.out_write = -1;

	pepa_core_unlock();
	counter++;

	if (counter > 1) {
		exit(0);
	}
}

void pepa_kill_all_threads(void)
{
	static int  counter = 0;
	pepa_core_t *core   = pepa_get_core();

	DD("#############################################\n");
	DD("#############################################\n");
	DD("#############################################\n");
	DD("##       BACK TO DISCONNECTED STATE        ##\n");
	DD("#############################################\n");
	DD("#############################################\n");
	DD("#############################################\n");

	pepa_core_lock();

	/*** Terminate threads ***/

	//pepa_thread_kill_ctl();
	pepa_thread_kill_out();
	pepa_thread_kill_in();
	pepa_thread_kill_shva();

	/*** Close the rest of the sockets ****/

	/* Close the IN socket */
	pepa_socket_shutdown_and_close(core->sockets.in_listen, "core->in_thread.fd_listen");
	core->sockets.in_listen = -1;

	/* Close the SHVA write socket */
	pepa_socket_close(core->sockets.shva_rw, "core->shva_thread.fd_write");
	core->sockets.shva_rw = -1;

	/* Close the OUT listen socket */
	pepa_socket_close(core->sockets.out_write, "core->shva_thread.fd_write");
	core->sockets.out_write = -1;

	/* Close the OUT listen socket */
	pepa_socket_shutdown_and_close(core->sockets.out_listen, "core->shva_thread.fd_write");
	core->sockets.out_listen = -1;


	pepa_core_unlock();
	counter++;

	if (counter > 1) {
		exit(0);
	}
}

#if 0 /* SEB */
const char *pepa_pr_str(pepa_proc_t p){
	switch (p) {
		case PEPA_PR_OUT:
		return "PEPA_PR_OUT";
		case PEPA_PR_SHVA:
		return "PEPA_PR_SHVA";
		case PEPA_PR_IN:
		return "PEPA_PR_IN";
		case PEPA_PR_CLT :
		return "PEPA_PR_CLT";
		case PEPA_PR_MAX:
		return "PEPA_PR_MAX";
	}

	return "NA";
}
#endif

const char *pepa_sig_str(pepa_sig_t p)
{
	switch (p) {
	case PEPA_ST_NONE:
		return "PEPA_ST_NONE";
	case PEPA_ST_RUN:
		return "PEPA_ST_RUN";
	case PEPA_ST_FAIL:
		return "PEPA_ST_FAIL";
	case PEPA_ST_SOCKET_RESET :
		return "PEPA_ST_SOCKET_RESET";
	case PEPA_ST_MAX:
		return "PEPA_ST_MAX";
	}

	return "NA";
}

/* Wait on condition */
void pepa_state_wait(pepa_core_t *core)
{
	pthread_mutex_lock(&core->state.sync_sem);
	pthread_cond_wait(&core->state.sync, &core->state.sync_sem);
	pthread_mutex_unlock(&core->state.sync_sem);
}

/* Send the condition change to all listeners */
void pepa_state_sig(pepa_core_t *core)
{
	pthread_mutex_lock(&core->state.sync_sem);
	pthread_cond_broadcast(&core->state.sync);
	pthread_mutex_unlock(&core->state.sync_sem);
}

void pepa_state_shva_set(pepa_core_t *core, pepa_sig_t sig)
{
	pthread_mutex_lock(&core->state.signals_sem);
	core->state.signals[PEPA_PR_SHVA] = sig;
	pthread_mutex_unlock(&core->state.signals_sem);
	pepa_state_sig(core);
	DDD("Set SHVA state to %s\n", pepa_sig_str(sig));
}

void pepa_state_in_set(pepa_core_t *core, pepa_sig_t sig)
{
	pthread_mutex_lock(&core->state.signals_sem);
	core->state.signals[PEPA_PR_IN] = sig;
	pthread_mutex_unlock(&core->state.signals_sem);
	pepa_state_sig(core);
	DDD("Set IN state to %s\n", pepa_sig_str(sig));
}

void pepa_state_out_set(pepa_core_t *core, pepa_sig_t sig)
{
	pthread_mutex_lock(&core->state.signals_sem);
	core->state.signals[PEPA_PR_OUT] = sig;
	pthread_mutex_unlock(&core->state.signals_sem);
	pepa_state_sig(core);
	DDD("Set OUT state to %s\n", pepa_sig_str(sig));
}



int pepa_state_shva_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_SHVA];
	pthread_mutex_unlock(&core->state.signals_sem);
	DDD("Return SHVA state is %s\n", pepa_sig_str(st));
	return st;
}

int pepa_state_in_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_IN];
	pthread_mutex_unlock(&core->state.signals_sem);
	DDD("Return IN state is %s\n", pepa_sig_str(st));
	return st;
}

int pepa_state_out_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_OUT];
	pthread_mutex_unlock(&core->state.signals_sem);
	DDD("Return OUT state is %s\n", pepa_sig_str(st));
	return st;
}

