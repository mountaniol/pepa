#include <errno.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/select.h>

#include "slog/src/slog.h"
#include "buf_t/se_debug.h"
#include "pepa_config.h"
#include "pepa_ip_struct.h"
#include "pepa_core.h"
#include "pepa_errors.h"
#include "pepa_parser.h"
#include "pepa_socket_common.h"
#include "pepa_version.h"
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

#define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

/* Catch Signal Handler functio */
static void signal_callback_handler(int signum)
{
	//printf("Caught signal SIGPIPE %d\n", signum);
	if (signum == SIGINT) {
		exit(0);
	}
}

static void main_set_sig_handler(void)
{
	sigset_t set;
	sigfillset(&set);

	int rc = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if (rc != 0) {
		handle_error_en(rc, "pthread_sigmask");
		exit(-1);
	}

	rc = sigprocmask(SIG_SETMASK, &set, NULL);
	if (rc != 0) {
		handle_error_en(rc, "process_mask");
		exit(-1);
	}

	signal(SIGPIPE, signal_callback_handler);
	signal(SIGINT, signal_callback_handler);
}

int pepa_config_slogger(pepa_core_t *core)
{
	slog_config_t cfg;
	slog_config_get(&cfg);

	cfg.nTraceTid = 1;
	cfg.eDateControl = SLOG_DATE_FULL;

	if (NULL != core->slog_file) {
		cfg.nToFile = 1;
		cfg.nKeepOpen = 1;
		cfg.nFlush = 1;
		strcpy(cfg.sFileName, core->slog_file);
	} else {
		slogn("No log file given");
	}

	 if (NULL != core->slog_dir) {
		 strcpy(cfg.sFilePath, core->slog_dir);
	 }

	if (0 == core->slog_print) {
		cfg.nToScreen = 0;
	}

	slog_config_set(&cfg);
	slog_enable(SLOG_TRACE);
	//slog_init("pepa", core->slog_level, 1);
	slog_config_set(&cfg);
	return PEPA_ERR_OK;
}

int main(int argi, char *argv[])
{
	int rc;
//	atexit(bye);

	//printf("pepa-ng version %d.%d.%d/%s\n", PEPA_VERSION_MAJOR, PEPA_VERSION_MINOR, PEPA_VERSION_PATCH, PEPA_VERSION_GIT);
	slog_init("pepa", SLOG_FLAGS_ALL, 0);
	pepa_print_version();

	slogn("Going to init core");
	rc = pepa_core_init();
	if (PEPA_ERR_OK != rc) {
		slog_fatal("Can not init core");
		abort();
	}
	slogn("Core inited");

	pepa_core_t *core = pepa_get_core();


	slogn("Going to parse arguments");
	rc = pepa_parse_arguments(argi, argv);
	if (rc < 0) {
		slog_fatal("Could not parse arguments: %s", pepa_error_code_to_str(rc));
		exit(-11);
	}

	pepa_config_slogger(core);
	slogn("Arguments parsed");

	main_set_sig_handler();

	slogn("Going to start threads");
	rc = pepa_start_threads();
	if (rc < 0) {
		slog_fatal("Could not start threads");
		exit(-11);
	}

	slogn("Threads are started");


	while (1) {
		sleep(120);
	}
	pepa_kill_all_threads();
	sleep(1);
	slogn("PEPA Exit");
	return (0);
}

