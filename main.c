#include <unistd.h>
#include <sys/param.h>
#include <errno.h>

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

#if 0 /* SEB */ /* 12.10.2024 */
void pepa_clean_on_exit(void){
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
	pepa3_close_sockets(core);

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
#endif /* SEB */ /* 12.10.2024 */

/* Catch Signal Handler function */
static void signal_callback_handler(int signum, __attribute__((unused)) siginfo_t *info, __attribute__((unused))void *extra)
{
	//pepa_core_t *core = pepa_get_core();
	printf("Caught signal %d\n", signum);
	if (signum == SIGINT) {
		printf("Caught signal SIGINT: %d\n", signum);
		//pepa_back_to_disconnected_state_new(core);
		pepa_clean_on_exit();
		exit(0);
	}

	if (signum == SIGUSR1) {
		printf("Caught signal SIGINT: %d\n", signum);
		//pepa_back_to_disconnected_state_new(core);
		pepa_clean_on_exit();
		exit(0);
	}
}
#if 0 /* SEB */ /* 12.10.2024 */

static void pepa_set_int_signal_handler(void){
	struct sigaction action;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	action.sa_flags = SA_SIGINFO | SIGUSR1;
	action.sa_sigaction = signal_callback_handler;
	sigaction(SIGINT, &action, NULL);
}
#endif /* SEB */ /* 12.10.2024 */

int pepa_go(pepa_core_t *core);



int main(int argi, char *argv[])
{
	int rc;

	logger_set_off();
	logger_set_level(LOGGER_NOISY);
	rc = logger_start();
	if (rc) {
		printf("Could not start PEPA logger\n");
		return -1;
	}

	slog_init("pepa", SLOG_FLAGS_ALL, 0);
	pepa_print_version();

	slog_note_l("Going to init core");
	rc = pepa_core_init();
	if (PEPA_ERR_OK != rc) {
		slog_fatal_l("Can not init core");
		abort();
	}
	slog_note_l("Core inited");

	pepa_core_t *core = pepa_get_core();

	rc = pepa_parse_arguments(argi, argv);
	if (rc < 0) {
		slog_fatal_l("Could not parse arguments: %s", pepa_error_code_to_str(rc));
		exit(-11);
	}

    if (NULL != core->config) {
        rc = pepa_read_config(core);
    }

	pepa_config_slogger(core);

	slog_note_l("Arguments parsed");
	logger_set_on();

	if (core->daemon) {
		daemonize(core);
		/* Set hight limit of opened files */
		/* After demonization we must reinit the logger */
		//slog_init("pepa", SLOG_FLAGS_ALL, 0);
		//rc = pepa_config_slogger_daemon(core);
	}
	pepa_set_rlimit();

	pepa_set_int_signal_handler(signal_callback_handler);

	//rc = pepa_start_threads(core);
	if (core->monitor.onoff) {
		slog_debug_l("Going to start MONITOR");
		pepa_thread_start_monitor(core);
	}

	slog_note_l("Going to start transfer loop");
	rc = pepa_go(core);
	if (rc < 0) {
		slog_fatal_l("Could not start threads");
		exit(-11);
	}

	while (1) {
		sleep(60);
	}
	//pepa_kill_all_threads(core);
	//sleep(1);
	pepa_core_release(core);
	slog_note_l("PEPA Exit");
	return (0);
}

