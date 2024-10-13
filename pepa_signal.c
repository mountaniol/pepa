#define _POSIX_C_SOURCE (199506L)
#include <stdlib.h>
#include <signal.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>

#include "slog/src/slog.h"
#include "pepa_core.h"
#include "pepa_config.h"
#include "pepa3.h"
#include "pepa_errors.h"
#include "pepa_parser.h"
#include "pepa_server.h"
#include "pepa_state_machine.h"
#include "logger.h"
#include "pepa_signal.h"

void pepa_set_int_signal_handler(void *callback)
{
	struct sigaction action;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	action.sa_flags = SA_SIGINFO | SIGUSR1;
	action.sa_sigaction = callback;
	sigaction(SIGINT, &action, NULL);
}

void pepa_clean_on_exit(void)
{
	pepa_core_t *core = pepa_get_core();

	slog_warn_l("=============================================");
	slog_warn_l("=============================================");
	slog_warn_l("Finishing PEPA");

	/* Remove PID file */
	if (NULL != core->pid_file_name) {
		int rc;
		slog_warn_l("Removing PID file %s", core->pid_file_name);
		rc = unlink(core->pid_file_name);
		if (0 != rc) {
			slog_error_l("Can not remove PID file: %s: %s", core->pid_file_name, strerror(errno));
		} else {
			slog_warn_l("Removed PID file: %s", core->pid_file_name);
		}

		//free(core->pid_file_name);
	}

	/* Terminate monithor thread */
	if (0 != core->monitor.onoff) {
		slog_warn_l("Terminating the monitor thread");
		pepa_thread_kill_monitor(core);
	}

	/* Close all sockets, if not closed */
	slog_warn_l("Closing all sockets");
	#ifdef pepa3_close_sockets
	pepa3_close_sockets(core);
	#endif

	pepa_core_release(core);

	/* Free buffers */
	//slog_warn_l("Freeing all buffers");
	//if (NULL != core->buffer) {
	//	free(core->buffer);
	//}

	/* Close slogger */
	slog_warn_l("Destroying the logger");
	slog_warn_l("=============================================");
	slog_warn_l("=============================================");
	slog_destroy();
}

