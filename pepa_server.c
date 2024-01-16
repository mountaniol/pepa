#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <sys/time.h>
#include <sys/resource.h>


#include "slog/src/slog.h"
#include "pepa_parser.h"
#include "pepa_core.h"

void pepa_set_rlimit(void)
{
	struct rlimit limit;

	limit.rlim_cur = 65535;
	limit.rlim_max = 65535;
	if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
		slog_debug_l("setrlimit() failed with errno=%d\n", errno);
	}

	/* Get max number of files. */
	if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
		slog_debug_l("getrlimit() failed with errno=%d\n", errno);
	}

	slog_debug_l("The soft limit is %lu\n", limit.rlim_cur);
	slog_debug_l("The hard limit is %lu\n", limit.rlim_max);
}

void daemonize(pepa_core_t *core)
{
	pid_t   pid = 0;
	int32_t fd;
	int32_t rc;

	/* Fork off the parent process */
	pid = fork();

	/* An error occurred */
	if (pid < 0) {
		slog_fatal_l("A fork error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* On success: The child process becomes session leader */
	if (setsid() < 0) {
		slog_fatal_l("A setsid error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Ignore signal sent from child to parent process */
	signal(SIGCHLD, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();

	/* An error occurred */
	if (pid < 0) {
		slog_fatal_l("A fork error");
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Set new file permissions */
	/* NO errors are defined for this function */
	umask(0);

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	rc = chdir("/tmp/");
	if (0 != rc) {
		slog_fatal_l("Can not change dir: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Close all open file descriptors */
	for (fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--) {
		close(fd);
	}

	/* Reopen stdin (fd = 0), stdout (fd = 1), stderr (fd = 2) */
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w+");
	stderr = fopen("/dev/null", "w+");

	//slog_init("pepa", SLOG_FLAGS_ALL, 0);
	rc = pepa_config_slogger_daemon(core);

	/* Try to write PID of daemon to lockfile */
	if (core->pid_file_name != NULL) {
		slog_note_l("Going to create PEPA PID file: %s", core->pid_file_name);
		char str[256];
		core->pid_fd = open(core->pid_file_name, O_RDWR | O_CREAT, 0640);
		if (core->pid_fd < 0) {
			/* Can't open lockfile */
			slog_fatal_l("Can not open lock file: %s", strerror(errno));
			exit(EXIT_FAILURE);
		} else {
			int err = errno;
			slog_error_l("Can not create PEPA PID file: %s, %s", core->pid_file_name, strerror(err));
			return;
		}

		if (lockf(core->pid_fd, F_TLOCK, 0) < 0) {
			/* Can't lock file */
			slog_fatal_l("Can not lock PID file: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}
		/* Get current PID */
		sprintf(str, "%d\n", getpid());
		/* Write PID to lockfile */
		rc = write(core->pid_fd, str, strlen(str));
		if (rc != (int32_t)strlen(str)) {
			slog_fatal_l("Can not write PID into PID file: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}
		
		slog_note_l("Created PEPA PID file: %s, the PID is %s", core->pid_file_name, str);
		// close(core->pid_fd);
	}
}

