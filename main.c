#include <unistd.h>
#include <sys/param.h>

#include "slog/src/slog.h"
#include "pepa_core.h"
#include "pepa_errors.h"
#include "pepa_parser.h"
#include "pepa_server.h"
#include "pepa_state_machine.h"

/* Catch Signal Handler function */
static void signal_callback_handler(int signum, __attribute__((unused)) siginfo_t *info, __attribute__((unused))void *extra)
{
	//pepa_core_t *core = pepa_get_core();
	printf("Caught signal %d\n", signum);
	if (signum == SIGINT) {
		printf("Caught signal SIGINT: %d\n", signum);
		//pepa_back_to_disconnected_state_new(core);
		exit(0);
	}

	if (signum == SIGUSR1) {
		printf("Caught signal SIGINT: %d\n", signum);
		//pepa_back_to_disconnected_state_new(core);
		exit(0);
	}
}

static void pepa_set_int_signal_handler(void)
{
	struct sigaction action;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	action.sa_flags = SA_SIGINFO | SIGUSR1;
	action.sa_sigaction = signal_callback_handler;
	sigaction(SIGINT, &action, NULL);
}

int pepa_go(pepa_core_t *core);

int main(int argi, char *argv[])
{
	int rc;

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

	slog_note_l("Going to parse arguments");
	rc = pepa_parse_arguments(argi, argv);
	if (rc < 0) {
		slog_fatal_l("Could not parse arguments: %s", pepa_error_code_to_str(rc));
		exit(-11);
	}

	rc = pepa_config_slogger(core);
	if (PEPA_ERR_OK != rc) {
		slog_error_l("Could not init slogger");
		exit(1);
	}

	slog_note_l("Arguments parsed");

	if (core->daemon) {
		daemonize(core);
		/* Set hight limit of opened files */
		pepa_set_rlimit();
		/* After demonization we must reinit the logger */
		slog_init("pepa", SLOG_FLAGS_ALL, 0);
		rc = pepa_config_slogger_daemon(core);
	}

	pepa_set_int_signal_handler();

	slog_note_l("Going to start threads");
	//rc = pepa_start_threads(core);
	if (core->monitor.onoff) {
		slog_debug_l("Going to start MONITOR");
		pepa_thread_start_monitor(core);
	}
	rc = pepa_go(core);
	if (rc < 0) {
		slog_fatal_l("Could not start threads");
		exit(-11);
	}

	slog_note_l("Threads are started");

	while (1) {
		sleep(60);
	}
	//pepa_kill_all_threads(core);
	//sleep(1);
	slog_note_l("PEPA Exit");
	return (0);
}

