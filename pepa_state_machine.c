#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_socket_common.h"
#include "pepa_state_machine.h"

void pepa_thread_cancel(const pthread_t pid, const char *name)
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

	/* Blocking wait until the pthread is really dead */

	rc = pthread_kill(pid, 0);

	while (0 == rc) {
		usleep(10);
		rc = pthread_kill(pid, 0);
	};

}

static int32_t pepa_thread_is_monitor_up(const pepa_core_t *core)
{
	if (PTHREAD_DEAD == core->monitor_thread.thread_id ||
		pthread_kill(core->monitor_thread.thread_id, 0) < 0) {
		return -PEPA_ERR_THREAD_DEAD;
	}
	return PEPA_ERR_OK;
}

void pepa_thread_kill_monitor(pepa_core_t *core)
{
	pepa_thread_cancel(core->monitor_thread.thread_id, "IN");
	core->monitor_thread.thread_id = PTHREAD_DEAD;
	slog_debug("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
	slog_debug("@@       THREAD <MONITOR> IS KILLED        @@");
	slog_debug("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
}

void *pepa_monitor_thread(__attribute__((unused))void *arg);

void pepa_thread_start_monitor(pepa_core_t *core)
{
	slog_note_l("Starting MONITOR thread");
	if (PEPA_ERR_OK == pepa_thread_is_monitor_up(core)) {
		slog_warn_l("Thread MONITOR is UP already, finishing");
		return;
	}
	int rc = pthread_create(&core->monitor_thread.thread_id, NULL, pepa_monitor_thread, NULL);
	if (0 != rc) {
		pepa_parse_pthread_create_error(rc);
		abort();
	}
	slog_debug("#############################################");
	slog_debug("##       THREAD <MONITOR> IS STARTED       ##");
	slog_debug("#############################################");
}

void *pepa_monitor_thread(__attribute__((unused))void *arg)
{
	const char *my_name          = "MONITOR";
	int        i;

//	pepa_set_int_signal_handler();

	int32_t    rc                = pepa_pthread_init_phase(my_name);
	if (rc < 0) {
		slog_fatal_l("%s: Could not init the thread", my_name);
		pthread_exit(NULL);
	}

	pepa_core_t *core        = pepa_get_core();
	pepa_stat_t monitor_prev;
	memcpy(&monitor_prev, &core->monitor, sizeof(monitor_prev));

	do {
		int        active_in_readers;

		if (core->monitor_divider_str[0] == 0) {
			slog_debug("### STATUS: /Units: %d bytes/ SHVA [+%lu | %lu/sec] ---> OUT [+%lu | %lu/sec] ### IN [+%lu | %lu/sec] ---> SHVA [+%lu | %lu/sec]  ###",
					   core->monitor_divider,
					   (core->monitor.shva_rx - monitor_prev.shva_rx) / (uint64_t)core->monitor_divider,
					   ((core->monitor.shva_rx - monitor_prev.shva_rx) / (uint64_t)core->monitor_freq) / (uint64_t)core->monitor_divider,

					   (core->monitor.out_tx - monitor_prev.out_tx) / (uint64_t)core->monitor_divider,
					   ((core->monitor.out_tx - monitor_prev.out_tx) / (uint64_t)core->monitor_freq) / (uint64_t)core->monitor_divider,

					   (core->monitor.in_rx - monitor_prev.in_rx) / (uint64_t)core->monitor_divider,
					   ((core->monitor.in_rx - monitor_prev.in_rx) / (uint64_t)core->monitor_freq) / (uint64_t)core->monitor_divider,

					   (core->monitor.shva_tx - monitor_prev.shva_tx) / (uint64_t)core->monitor_divider,
					   ((core->monitor.shva_tx - monitor_prev.shva_tx) / (uint64_t)core->monitor_freq) / (uint64_t)core->monitor_divider
					  );
		} else {
			char *divider_str = "Unknown";

			if (core->monitor_divider_str[0] == 'K') {
				divider_str = "Kbytes";
			}

			if (core->monitor_divider_str[0] == 'M') {
				divider_str = "Mbytes";
			}
			slog_debug("### STATUS: Freq: %u seconds, Units: %s ### SHVA [+%lu | %lu/sec] ---> OUT [+%lu | %lu/sec] ### IN [+%lu | %lu/sec] ---> SHVA [+%lu | %lu/sec]  ###",
					   core->monitor_freq, divider_str,
					   (core->monitor.shva_rx - monitor_prev.shva_rx) / (uint64_t)core->monitor_divider,
					   ((core->monitor.shva_rx - monitor_prev.shva_rx) / (uint64_t)core->monitor_freq) / (uint64_t)core->monitor_divider,

					   (core->monitor.out_tx - monitor_prev.out_tx) / (uint64_t)core->monitor_divider,
					   ((core->monitor.out_tx - monitor_prev.out_tx) / (uint64_t)core->monitor_freq) / (uint64_t)core->monitor_divider,

					   (core->monitor.in_rx - monitor_prev.in_rx) / (uint64_t)core->monitor_divider,
					   ((core->monitor.in_rx - monitor_prev.in_rx) / (uint64_t)core->monitor_freq) / (uint64_t)core->monitor_divider,

					   (core->monitor.shva_tx - monitor_prev.shva_tx) / (uint64_t)core->monitor_divider,
					   ((core->monitor.shva_tx - monitor_prev.shva_tx) / (uint64_t)core->monitor_freq) / (uint64_t)core->monitor_divider
					  );
		}

		active_in_readers = 0;
		if (NULL != core->in_reading_sockets.sockets) {
			for (i = 0; i < core->in_reading_sockets.number; i++) {
				if (core->in_reading_sockets.sockets[i] > -1) {
					active_in_readers++;
				}
			}
		}

		slog_debug("### STATUS: Sockets: OUT LISTEN FD[%d]: %d | OUT WRITE FD[%d]: %d | SHVA FD[%d]: %d | IN LISTEN FD[%d]: %d | IN ACCEPTORS[%d] ###",
				   (core->sockets.out_listen >= 0) ? 1 : 0,
				   core->sockets.out_listen,

				   (core->sockets.out_write >= 0) ? 1 : 0,
				   core->sockets.out_write,

				   (core->sockets.shva_rw >= 0) ? 1 : 0,
				   core->sockets.shva_rw,

				   (core->sockets.in_listen >= 0) ? 1 : 0,
				   core->sockets.in_listen,

				   active_in_readers);
		slog_debug("### STATUS:  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");

		fflush(stdout);

		memcpy(&monitor_prev, &core->monitor, sizeof(monitor_prev));
		sleep(core->monitor_freq);
	} while (1);
}

