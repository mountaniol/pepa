#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_socket_common.h"

static void pepa_thread_cancel(pthread_t pid, const char *name)
{
	if (PTHREAD_DEAD == pid || pthread_kill(pid, 0) < 0) {
		slog_error("Can not cancel %s thread, it is not alive", name);
		return;
	}

	int rc = pthread_cancel(pid);
	if (0 != rc) {
		slog_fatal("Can not cancel <%s> thread", name);
	} else {
		slog_note("## Canceled <%s> thread", name);
	}
}

static void pepa_socket_close(int fd, const char *socket_name)
{
	if (fd < 0) {
		slog_error("Can not close socket %s, its value is %d", socket_name, fd);
		return;
	}

	int rc = close(fd);
	if (0 != rc) {
		slog_error("Can not close socket %s, error %d:%s", socket_name, rc, strerror(errno));
		return;
	}

	slog_note("## Closed socket socket %s");
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

	slog_note("core->in_thread.thread_id = %lX", core->in_thread.thread_id);
	if (PTHREAD_DEAD == core->in_thread.thread_id) {
		slog_error("THREAD IN IS DEAD: core->in_thread.thread_id = %lX", core->in_thread.thread_id);
		return -PEPA_ERR_THREAD_DEAD;
	}
	if (pthread_kill(core->in_thread.thread_id, 0) < 0) {
		slog_error("THREAD IN IS DEAD: kill = %d", pthread_kill(core->in_thread.thread_id, 0));
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

	slog_debug("#############################################");
	slog_debug("##       THREAD <SHVA> IS KILLED           ##");
	slog_debug("#############################################");
}

void pepa_thread_kill_out(void)
{
	pepa_core_t          *core = pepa_get_core();
	pepa_thread_cancel(core->out_thread.thread_id, "OUT");
	core->out_thread.thread_id = PTHREAD_DEAD;

	slog_debug("#############################################");
	slog_debug("##       THREAD <OUT> IS KILLED            ##");
	slog_debug("#############################################");
}

void pepa_thread_kill_in(void)
{
	pepa_core_t          *core = pepa_get_core();
	pepa_thread_cancel(core->in_thread.thread_id, "IN");
	core->in_thread.thread_id = PTHREAD_DEAD;

	slog_debug("#############################################");
	slog_debug("##       THREAD <IN> IS KILLED             ##");
	slog_debug("#############################################");
}

void pepa_thread_start_out(void)
{
	pepa_core_t          *core = pepa_get_core();
	slog_note("Starting OUT thread");
	if (PEPA_ERR_OK == pepa_thread_is_out_up()) {
		slog_note("Thread OUT is UP already, finishing");
		return;
	}
	int rc    = pthread_create(&core->out_thread.thread_id, NULL, pepa_out_thread, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}

	slog_debug("#############################################");
	slog_debug("##       THREAD OUT IS STARTED              ##");
	slog_debug("#############################################");

}

void pepa_thread_start_shva(void)
{
	pepa_core_t          *core = pepa_get_core();
	slog_note("Starting SHVA thread");
	if (PEPA_ERR_OK == pepa_thread_is_shva_up()) {
		slog_note("Thread SHVA is UP already, finishing");
		return;
	}

	int rc    = pthread_create(&core->shva_thread.thread_id, NULL, pepa_shva_thread_new, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}

	slog_debug("#############################################");
	slog_debug("##       THREAD SHVA IS STARTED            ##");
	slog_debug("#############################################");

}

void pepa_thread_start_in(void)
{
	pepa_core_t          *core = pepa_get_core();
	slog_note("Starting IN thread");
	if (PEPA_ERR_OK == pepa_thread_is_in_up()) {
		slog_note("Thread IN is UP already, finishing");
		return;
	}

	int rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_in_thread_new, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}

	slog_debug("#############################################");
	slog_debug("##       THREAD IN IS STARTED              ##");
	slog_debug("#############################################");
}

void pepa_back_to_disconnected_state_new(void)
{
	static int  counter = 0;
	pepa_core_t *core   = pepa_get_core();

	slog_debug("#############################################");
	slog_debug("##       BACK TO DISCONNECTED STATE        ##");
	slog_debug("#############################################");

	pepa_core_lock();

	/*** Terminate threads ***/

	pepa_thread_kill_out();
	pepa_thread_kill_in();
	pepa_thread_kill_shva();

	/*** Close the rest of the sockets ****/

	/* Close the IN socket */
	pepa_socket_close(core->sockets.in_listen, "core->sockets.in_listen");
	core->sockets.in_listen = -1;

	slog_debug("##       BACK TO DISCONNECTED STATE        ##");

	/* Close the SHVA write socket */
	pepa_socket_close(core->sockets.shva_rw, "core->sockets.shva_rw");
	core->sockets.shva_rw = -1;

	/* Close the OUT listen socket */
	pepa_socket_close(core->sockets.out_listen, "core->sockets.out_listen");
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

	slog_debug("#############################################");
	slog_debug("#############################################");
	slog_debug("#############################################");
	slog_debug("##       BACK TO DISCONNECTED STATE        ##");
	slog_debug("#############################################");
	slog_debug("#############################################");
	slog_debug("#############################################");

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
	pepa_socket_shutdown_and_close(core->sockets.shva_rw, "core->shva_thread.fd_write");
	core->sockets.out_listen = -1;

	/* Close the OUT write socket */
	pepa_socket_close(core->sockets.out_write, "core->shva_thread.fd_write");
	core->sockets.shva_rw = -1;

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
	slog_note("Set SHVA state to %s", pepa_sig_str(sig));
}

void pepa_state_in_set(pepa_core_t *core, pepa_sig_t sig)
{
	pthread_mutex_lock(&core->state.signals_sem);
	core->state.signals[PEPA_PR_IN] = sig;
	pthread_mutex_unlock(&core->state.signals_sem);
	pepa_state_sig(core);
	slog_note("Set IN state to %s", pepa_sig_str(sig));
}

void pepa_state_out_set(pepa_core_t *core, pepa_sig_t sig)
{
	pthread_mutex_lock(&core->state.signals_sem);
	core->state.signals[PEPA_PR_OUT] = sig;
	pthread_mutex_unlock(&core->state.signals_sem);
	pepa_state_sig(core);
	slog_note("Set OUT state to %s", pepa_sig_str(sig));
}



int pepa_state_shva_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_SHVA];
	pthread_mutex_unlock(&core->state.signals_sem);
	slog_note("Return SHVA state is %s", pepa_sig_str(st));
	return st;
}

int pepa_state_in_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_IN];
	pthread_mutex_unlock(&core->state.signals_sem);
	slog_note("Return IN state is %s", pepa_sig_str(st));
	return st;
}

int pepa_state_out_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_OUT];
	pthread_mutex_unlock(&core->state.signals_sem);
	slog_note("Return OUT state is %s", pepa_sig_str(st));
	return st;
}

int pepa_start_threads(void)
{
	//set_sig_handler();

	/* Slose STDIN */
	int fd = open("/dev/null", O_WRONLY);
	dup2(fd, 0);
	close(fd);

	/* Start CTL thread */
	// pepa_thread_start_ctl();

	/* Start SHVA thread */
	//pepa_shva_start();
	pepa_thread_start_out();
	pepa_thread_start_shva();
	pepa_thread_start_in();
	return PEPA_ERR_OK;
}
