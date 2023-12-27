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
		slog_error_l("Can not cancel %s thread, it is not alive", name);
		return;
	}

	int rc = pthread_cancel(pid);
	if (0 != rc) {
		slog_fatal_l("Can not cancel <%s> thread", name);
	} else {
		slog_note_l("## Canceled <%s> thread", name);
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
int pepa_thread_is_shva_fw_up(void)
{
	pepa_core_t          *core = pepa_get_core();
	if (PTHREAD_DEAD == core->shva_forwarder.thread_id ||
		pthread_kill(core->shva_forwarder.thread_id, 0) < 0) {
		return -PEPA_ERR_THREAD_DEAD;
	}
	return PEPA_ERR_OK;
}
int pepa_thread_is_monitor_up(void)
{
	pepa_core_t          *core = pepa_get_core();
	if (PTHREAD_DEAD == core->monitor_thread.thread_id ||
		pthread_kill(core->monitor_thread.thread_id, 0) < 0) {
		return -PEPA_ERR_THREAD_DEAD;
	}
	return PEPA_ERR_OK;
}

int pepa_thread_is_in_up(void)
{
	pepa_core_t          *core = pepa_get_core();

	slog_note_l("core->in_thread.thread_id = %lX", core->in_thread.thread_id);
	if (PTHREAD_DEAD == core->in_thread.thread_id) {
		slog_error_l("THREAD IN IS DEAD: core->in_thread.thread_id = %lX", core->in_thread.thread_id);
		return -PEPA_ERR_THREAD_DEAD;
	}
	if (pthread_kill(core->in_thread.thread_id, 0) < 0) {
		slog_error_l("THREAD IN IS DEAD: kill = %d", pthread_kill(core->in_thread.thread_id, 0));
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

void pepa_thread_kill_shva_forwarder(void)
{
	pepa_core_t          *core = pepa_get_core();
	pepa_thread_cancel(core->shva_forwarder.thread_id, "SHVA-FORWARD");
	core->shva_forwarder.thread_id = PTHREAD_DEAD;
	slog_debug("#############################################");
	slog_debug("##       THREAD <SHVA-FORWARD> IS KILLED   ##");
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
void pepa_thread_kill_monitor(void)
{
	pepa_core_t          *core = pepa_get_core();
	pepa_thread_cancel(core->monitor_thread.thread_id, "IN");
	core->monitor_thread.thread_id = PTHREAD_DEAD;
	slog_debug("#############################################");
	slog_debug("##       THREAD <MONITOR> IS KILLED        ##");
	slog_debug("#############################################");
}

void pepa_thread_start_out(void)
{
	pepa_core_t          *core = pepa_get_core();
	slog_note_l("Starting OUT thread");
	if (PEPA_ERR_OK == pepa_thread_is_out_up()) {
		slog_note_l("Thread OUT is UP already, finishing");
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
	slog_note_l("Starting SHVA thread");
	if (PEPA_ERR_OK == pepa_thread_is_shva_up()) {
		slog_note_l("Thread SHVA is UP already, finishing");
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
	slog_note_l("Starting IN thread");
	if (PEPA_ERR_OK == pepa_thread_is_in_up()) {
		slog_note_l("Thread IN is UP already, finishing");
		return;
	}

	int rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_in_thread, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}

	slog_debug("#############################################");
	slog_debug("##       THREAD IN IS STARTED              ##");
	slog_debug("#############################################");
}


void *pepa_monitor_thread(__attribute__((unused))void *arg);

void pepa_thread_start_monitor(void)
{
	pepa_core_t          *core = pepa_get_core();
	slog_note_l("Starting IN thread");
	if (PEPA_ERR_OK == pepa_thread_is_monitor_up()) {
		slog_warn_l("Thread IN is UP already, finishing");
		return;
	}
	int rc = pthread_create(&core->monitor_thread.thread_id, NULL, pepa_monitor_thread, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}
	slog_debug("#############################################");
	slog_debug("##       THREAD MONITOR IS STARTED         ##");
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
	pepa_socket_close_in_listen(core);
	slog_debug("##       BACK TO DISCONNECTED STATE        ##");

	/* Close the SHVA write socket */
	pepa_socket_close_shva_rw(core);

	/* Close the OUT listen socket */
	pepa_socket_close_out_listen(core);

	/* Close the OUT listen socket */
	pepa_socket_close_out_write(core);
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
	pepa_thread_kill_shva_forwarder();
	pepa_thread_kill_shva();
	pepa_thread_kill_monitor();

	/*** Close the rest of the sockets ****/

	/* Close the IN socket */
	pepa_socket_close_in_listen(core);

	/* Close the SHVA write socket */
	pepa_socket_close_shva_rw(core);

	/* Close the OUT listen socket */
	pepa_socket_close_out_listen(core);

	/* Close the OUT write socket */
	pepa_socket_close_out_write(core);

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
	slog_note_l("Set SHVA state to %s", pepa_sig_str(sig));
}

void pepa_state_in_set(pepa_core_t *core, pepa_sig_t sig)
{
	pthread_mutex_lock(&core->state.signals_sem);
	core->state.signals[PEPA_PR_IN] = sig;
	pthread_mutex_unlock(&core->state.signals_sem);
	pepa_state_sig(core);
	slog_note_l("Set IN state to %s", pepa_sig_str(sig));
}

void pepa_state_out_set(pepa_core_t *core, pepa_sig_t sig)
{
	pthread_mutex_lock(&core->state.signals_sem);
	core->state.signals[PEPA_PR_OUT] = sig;
	pthread_mutex_unlock(&core->state.signals_sem);
	pepa_state_sig(core);
	slog_note_l("Set OUT state to %s", pepa_sig_str(sig));
}



int pepa_state_shva_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_SHVA];
	pthread_mutex_unlock(&core->state.signals_sem);
	slog_note_l("Return SHVA state is %s", pepa_sig_str(st));
	return st;
}

int pepa_state_in_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_IN];
	pthread_mutex_unlock(&core->state.signals_sem);
	slog_note_l("Return IN state is %s", pepa_sig_str(st));
	return st;
}

int pepa_state_out_get(pepa_core_t *core)
{
	int st;
	pthread_mutex_lock(&core->state.signals_sem);
	st = core->state.signals[PEPA_PR_OUT];
	pthread_mutex_unlock(&core->state.signals_sem);
	slog_note_l("Return OUT state is %s", pepa_sig_str(st));
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
	pepa_thread_start_monitor();
	return PEPA_ERR_OK;
}

#define KB(x) ((x)/1024)
#define MB(x) ((x)/(1024 * 1024))
#define MONITOR_SLEEP_TIME (5)
void *pepa_monitor_thread(__attribute__((unused))void *arg)
{
	const char *my_name = "MONITOR";
	int        rc       = pepa_pthread_init_phase(my_name);
	if (rc < 0) {
		slog_fatal_l("%s: Could not init the thread", my_name);
		pthread_exit(NULL);
	}

	pepa_core_t *core        = pepa_get_core();
	pepa_stat_t monitor_prev;
	memcpy(&monitor_prev, &core->monitor, sizeof(monitor_prev));

	do {
		int shva_st    = pepa_thread_is_shva_up();
		int shva_fw_st = pepa_thread_is_shva_fw_up();
		int out_st     = pepa_thread_is_out_up();
		int in_st      = pepa_thread_is_in_up();

#if 0 /* SEB */
		slog_debug("### STATUS: OUT %s, SHVA %s: SHVA FW: %s, IN: %s",
				   (PEPA_ERR_OK == out_st) ? "UP"  : "DOWN",
				   (PEPA_ERR_OK == shva_st) ? "UP"  : "DOWN",
				   (PEPA_ERR_OK == shva_fw_st) ? "UP"  : "DOWN",
				   (PEPA_ERR_OK == in_st) ? "UP"  : "DOWN");
#endif

#if 0 /* SEB */
		slog_debug("### STATUS: (Kb) OUT: %s TX: %lu [df: %lu] || SHVA: %s RX: %lu [df: %lu] TX: %lu [df: %lu] || SHVA FWD: %s || IN: %s RX: %lu [df: %lu] ###",
				   (PEPA_ERR_OK == out_st) ? "UP"  : "DOWN",
				   KB(core->monitor.out_tx),
				   KB(core->monitor.out_tx - monitor_prev.out_tx),

				   (PEPA_ERR_OK == shva_st) ? "UP"  : "DOWN",
				   KB(core->monitor.shva_rx),
				   KB(core->monitor.shva_rx - monitor_prev.shva_rx),

				   KB(core->monitor.shva_tx),
				   KB(core->monitor.shva_tx - monitor_prev.shva_tx),

				   (PEPA_ERR_OK == shva_fw_st) ? "UP"  : "DOWN",

				   (PEPA_ERR_OK == in_st) ? "UP"  : "DOWN",

				   KB(core->monitor.in_rx),
				   KB(core->monitor.in_rx - monitor_prev.in_rx));
#endif

#if 0 /* SEB */
		slog_debug("### STATUS: (Kb) SHVA FW [%s : %lu | +%lu] -> OUT [%s : %lu | +%lu] ### IN [%s : %lu | +%lu] -> SHVA [%s : %lu | +%lu]  ###",
				   (PEPA_ERR_OK == shva_fw_st) ? "UP"  : "DOWN"),
		KB(core->monitor.shva_rx),
		KB(core->monitor.shva_rx - monitor_prev.shva_rx),

		(PEPA_ERR_OK == out_st) ? "UP"  : "DOWN",
		KB(core->monitor.out_tx),
		KB(core->monitor.out_tx - monitor_prev.out_tx),

		KB(core->monitor.shva_tx),
		KB(core->monitor.shva_tx - monitor_prev.shva_tx),

		(PEPA_ERR_OK == in_st) ? "UP"  : "DOWN",
		KB(core->monitor.in_rx),
		KB(core->monitor.in_rx - monitor_prev.in_rx),

		(PEPA_ERR_OK == shva_st) ? "UP"  : "DOWN",
		KB(core->monitor.shva_tx),
		KB(core->monitor.shva_tx - monitor_prev.shva_tx),
#endif

		slog_debug("### STATUS: SHVA: %s | SHVA FW: %s | IN: %s | OUT: %s ###",
				   (PEPA_ERR_OK == shva_st) ? "UP"  : "DOWN",
				   (PEPA_ERR_OK == shva_fw_st) ? "UP"  : "DOWN",
				   (PEPA_ERR_OK == in_st) ? "UP"  : "DOWN",
				   (PEPA_ERR_OK == out_st) ? "UP"  : "DOWN");

#if 0 /* SEB */
		slog_debug("### STATUS: (Kb) SHVA FW [%lu | +%lu] ---> OUT [%lu | +%lu] ### IN [%lu | +%lu] ---> SHVA [%lu | +%lu]  ###",
				   KB(core->monitor.shva_rx),
				   KB(core->monitor.shva_rx - monitor_prev.shva_rx),

				   KB(core->monitor.out_tx),
				   KB(core->monitor.out_tx - monitor_prev.out_tx),

				   KB(core->monitor.shva_tx),
				   KB(core->monitor.shva_tx - monitor_prev.shva_tx),

				   KB(core->monitor.in_rx),
				   KB(core->monitor.in_rx - monitor_prev.in_rx),

				   KB(core->monitor.shva_tx),
				   KB(core->monitor.shva_tx - monitor_prev.shva_tx));
#endif

		slog_debug("### STATUS: (Kb) SHVA FW [+%lu | %lu / sec] ---> OUT [+%lu | %lu / sec] ### IN [+%lu | %lu / sec] ---> SHVA [+%lu | %lu / sec]  ###",
				   KB(core->monitor.shva_rx - monitor_prev.shva_rx),
				   KB((core->monitor.shva_rx - monitor_prev.shva_rx) / MONITOR_SLEEP_TIME),

				   KB(core->monitor.out_tx - monitor_prev.out_tx),
				   KB((core->monitor.out_tx - monitor_prev.out_tx)/ MONITOR_SLEEP_TIME ),

				   KB(core->monitor.shva_tx - monitor_prev.shva_tx),
				   KB((core->monitor.shva_tx - monitor_prev.shva_tx) / MONITOR_SLEEP_TIME),

				   KB(core->monitor.in_rx - monitor_prev.in_rx),
				   KB((core->monitor.in_rx - monitor_prev.in_rx ) / MONITOR_SLEEP_TIME),

				   KB(core->monitor.shva_tx - monitor_prev.shva_tx),
				   KB((core->monitor.shva_tx - monitor_prev.shva_tx) / MONITOR_SLEEP_TIME)
				   );


		slog_debug("### STATUS:  out_listen: %d | out_write: %d | shva_rw: %d | in_listen: %d ###",
				   core->sockets.out_listen, core->sockets.out_write, core->sockets.shva_rw, core->sockets.in_listen);
		slog_debug("### STATUS:  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");

		memcpy(&monitor_prev, &core->monitor, sizeof(monitor_prev));
		sleep(MONITOR_SLEEP_TIME);
	} while (1);
}

