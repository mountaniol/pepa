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
#include "pepa_socket_ctl.h"
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

	while (0 == pthread_kill(pid, 0)) {
		DD("Waiting thread %s to terminate...\n", name);
		usleep(1);
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

int pepa_thread_is_ctl_up(void)
{
	pepa_core_t          *core = pepa_get_core();
	if (PTHREAD_DEAD == core->ctl_thread.thread_id ||
		pthread_kill(core->ctl_thread.thread_id, 0) < 0) {
		return -1;
	}
	return PEPA_ERR_OK;
}

int pepa_thread_is_shva_up(void)
{
	pepa_core_t          *core = pepa_get_core();
	if (PTHREAD_DEAD == core->shva_thread.thread_id ||
		pthread_kill(core->shva_thread.thread_id, 0) < 0) {
		return -1;
	}
	return PEPA_ERR_OK;
}

int pepa_thread_is_in_up(void)
{
	pepa_core_t          *core = pepa_get_core();

	DD("core->in_thread.thread_id = %lX\n", core->in_thread.thread_id);
	if (PTHREAD_DEAD == core->in_thread.thread_id) {
		DD("THREAD IN IS DEAD: core->in_thread.thread_id = %lX\n", core->in_thread.thread_id);
		return -1;
	}
	if (pthread_kill(core->in_thread.thread_id, 0) < 0) {

		DD("THREAD IN IS DEAD: kill = %d\n", pthread_kill(core->in_thread.thread_id, 0));
		return -1;

	}
	return PEPA_ERR_OK;
}

int pepa_thread_is_out_up(void)
{
	pepa_core_t          *core = pepa_get_core();

	if (PTHREAD_DEAD == core->out_thread.thread_id ||
		pthread_kill(core->out_thread.thread_id, 0) < 0) {
		return -1;
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
	DD("##       THREAD CTL IS KILLED               ##\n");
	DD("#############################################\n");
}

void pepa_thread_start_ctl(void)
{
	pepa_core_t          *core = pepa_get_core();
	DDD("Starting CTL thread\n");
	int rc    = pthread_create(&core->ctl_thread.thread_id, NULL, pepa_ctl_thread_new, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}

	DD("#############################################\n");
	DD("##       THREAD CTL IS STARTED             ##\n");
	DD("#############################################\n");
}

void pepa_thread_start_out(void)
{
	pepa_core_t          *core = pepa_get_core();
	DDD("Starting OUT thread\n");
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

#if 0 /* SEB */
typedef enum {
	PEPA_PR_OUT = 0,
	PEPA_PR_SHVA,
	PEPA_PR_IN,
	PEPA_PR_CLT,
	PEPA_PR_MAX
}
pepa_proc_t;

typedef enum {
	PEPA_ST_NONE = 0,
	PEPA_ST_RUN,
	PEPA_ST_FAIL,
	PEPA_ST_SOCKET_RESET,
	PEPA_ST_MAX
}
pepa_sig_t;

typedef enum {
	PEPA_ACT_NONE = 0,
	PEPA_ACT_START_OUT = (1 << 0),
	PEPA_ACT_START_IN = (1 << 1),
	PEPA_ACT_START_SHVA = (1 << 2),
	PEPA_ACT_STOP_OUT = (1 << 3),
	PEPA_ACT_STOP_IN = (1 << 4),
	PEPA_ACT_STOP_SHVA = (1 << 5),
	PEPA_ACT_RESTART_ALL = (1 << 6),
	PEPA_ACT_ABORT = (1 << 7),
}
pepa_action_t;

#endif
const char *pepa_act_str(pepa_action_t p)
{
	switch (p) {
	case PEPA_ACT_NONE:
		return "PEPA_ACT_NONE";
	case PEPA_ACT_START_OUT:
		return "PEPA_ACT_START_OUT";
	case PEPA_ACT_START_IN:
		return "PEPA_ACT_START_IN";
	case PEPA_ACT_START_SHVA :
		return "PEPA_ACT_START_SHVA";
	case PEPA_ACT_STOP_OUT:
		return "PEPA_ACT_STOP_OUT";
	case PEPA_ACT_STOP_IN:
		return "PEPA_ACT_STOP_IN";
	case PEPA_ACT_STOP_SHVA:
		return "PEPA_ACT_STOP_SHVA";
	case PEPA_ACT_RESTART_ALL:
		return "PEPA_ACT_RESTART_ALL";
	case PEPA_ACT_ABORT:
		return "PEPA_ACT_ABORT";
	case PEPA_ACT_MAX:
		return "PEPA_ACT_MAX";
	}
	return "NA";
}

const char *pepa_pr_str(pepa_proc_t p)
{
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

static const int pepa_state_action[PEPA_PR_MAX][PEPA_ST_MAX] =
{
	/* OUT porcess actions */
	{
		/* {PEPA_PR_OUT}, {PEPA_ST_NONE}, == */ 			PEPA_ACT_NONE,
		/* {PEPA_PR_OUT}, {PEPA_ST_RUN}, == */				PEPA_ACT_START_SHVA,
		/* {PEPA_PR_OUT}, {PEPA_ST_FAIL}, == */ 			PEPA_ACT_RESTART_ALL,
		/* {PEPA_PR_OUT}, {PEPA_ST_SOCKET_RESET}, == */ 	PEPA_ACT_NONE,
	},

	{
		/* SHVA porcess actions */
		/* {PEPA_PR_SHVA}, {PEPA_ST_NONE}, == */ 			PEPA_ACT_NONE,
		/* {PEPA_PR_SHVA}, {PEPA_ST_RUN}, == */ 			PEPA_ACT_START_IN,
		/* {PEPA_PR_SHVA}, {PEPA_ST_FAIL}, == */ 			PEPA_ACT_RESTART_ALL,
		/* {PEPA_PR_SHVA}, {PEPA_ST_SOCKET_RESET}, == */ 	PEPA_ACT_NONE,
	},

	{
		/* IN porcess actions */
		/* {PEPA_PR_IN}, {PEPA_ST_NONE}, == */ 				PEPA_ACT_NONE,
		/* {PEPA_PR_IN}, {PEPA_ST_RUN}, == */ 				PEPA_ACT_NONE,
		/* {PEPA_PR_IN}, {PEPA_ST_FAIL}, == */ 				PEPA_ACT_RESTART_ALL,
		/* {PEPA_PR_IN}, {PEPA_ST_SOCKET_RESET}, == */ 		PEPA_ACT_NONE,
	},

	{
		/* CTL porcess actions: it should nevet emit signals */
		/* {PEPA_PR_CLT}, {PEPA_ST_NONE}, == */ 			PEPA_ACT_ABORT,
		/* {PEPA_PR_CLT}, {PEPA_ST_RUN}, == */ 				PEPA_ACT_ABORT,
		/* {PEPA_PR_CLT}, {PEPA_ST_FAIL}, == */				PEPA_ACT_ABORT,
		/* {PEPA_PR_CLT}, {PEPA_ST_SOCKET_RESET},  == */ 	PEPA_ACT_ABORT,
	}
};

void pepa_state_clear(pepa_core_t *core, int process)
{
	core->state.signals[process] = PEPA_ST_NONE;
}

void pepa_state_lock(pepa_core_t *core)
{
	pthread_mutex_lock(&core->state.sync_sem);
}

void pepa_state_unlock(pepa_core_t *core)
{
	pthread_mutex_unlock(&core->state.sync_sem);
}

void pepa_state_wait(pepa_core_t *core)
{
	pthread_mutex_lock(&core->state.sync_sem);
	pthread_cond_wait(&core->state.sync, &core->state.sync_sem);
	pthread_mutex_unlock(&core->state.sync_sem);
}

void pepa_state_sig(pepa_core_t *core)
{
	pthread_mutex_lock(&core->state.sync_sem);
	pthread_cond_signal(&core->state.sync);
	pthread_mutex_unlock(&core->state.sync_sem);
}

void pepa_state_set(pepa_core_t *core, int process, int state, const char *func, const int line)
{
	DD("Setting sig for process %s, state %s from %s line %d\n",
	   pepa_pr_str(process), pepa_sig_str(state), func, line);
	core->state.signals[process] = state;
	pepa_state_sig(core);
}

int pepa_state_get(pepa_core_t *core, int process)
{
	DD("Returning sig for process %s, state %s\n",
	   pepa_pr_str(process),
	   pepa_sig_str(core->state.signals[process]));
	return core->state.signals[process];
}

int pepa_state_to_action(int process, int state)
{
	return pepa_state_action[process][state];
}
