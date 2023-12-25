#include <unistd.h>
#include <sys/param.h>

#include "slog/src/slog.h"
#include "pepa_core.h"
#include "pepa_errors.h"
#include "pepa_parser.h"
#include "pepa_state_machine.h"

void bye(void)
{
	pepa_core_t *core = pepa_get_core();
	pthread_cancel(core->shva_thread.thread_id);
	pthread_cancel(core->out_thread.thread_id);

	/* This function closes all descriptors,
	   frees all buffers and also frees core struct */
	pepa_core_finish();
}

/* Catch Signal Handler functio */
static void signal_callback_handler(int signum, __attribute__((unused))siginfo_t *info, __attribute__((unused))void *extra)
{
	printf("Caught signal %d\n", signum);
	if (signum == SIGINT) {
		printf("Caught signal SIGINT: %d\n", signum);
		pepa_back_to_disconnected_state_new();
		exit(0);
	}
}

void pepa_set_int_signal_handler(void)
{
    struct sigaction action;

    action.sa_flags = SA_SIGINFO;     
    action.sa_sigaction = signal_callback_handler;
    sigaction(SIGINT, &action, NULL);
}

int main(int argi, char *argv[])
{
	int rc;

	slog_init("pepa", SLOG_FLAGS_ALL, 0);
	pepa_print_version();

	slog_note("Going to init core");
	rc = pepa_core_init();
	if (PEPA_ERR_OK != rc) {
		slog_fatal("Can not init core");
		abort();
	}
	slog_note("Core inited");

	pepa_core_t *core = pepa_get_core();

	slog_note("Going to parse arguments");
	rc = pepa_parse_arguments(argi, argv);
	if (rc < 0) {
		slog_fatal("Could not parse arguments: %s", pepa_error_code_to_str(rc));
		exit(-11);
	}

	pepa_config_slogger(core);
	slog_note("Arguments parsed");

	slog_note("Going to start threads");
	rc = pepa_start_threads();
	if (rc < 0) {
		slog_fatal("Could not start threads");
		exit(-11);
	}

	slog_note("Threads are started");

	pepa_set_int_signal_handler();

	while (1) {
		sleep(120);
	}
	pepa_kill_all_threads();
	sleep(1);
	slog_note("PEPA Exit");
	return (0);
}

