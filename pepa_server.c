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
	pid_t pid = 0;
	int   fd;

	slog_debug_l("1");

	/* Fork off the parent process */
	pid = fork();

	slog_debug_l("2");

	/* An error occurred */
	if (pid < 0) {
		slog_debug_l("A fork error");
		exit(EXIT_FAILURE);
	}

	slog_debug_l("3");

	/* Success: Let the parent terminate */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	slog_debug_l("4");

	/* On success: The child process becomes session leader */
	if (setsid() < 0) {
		slog_debug_l("A setsid error");
		exit(EXIT_FAILURE);
	}

	slog_debug_l("5");

	/* Ignore signal sent from child to parent process */
	signal(SIGCHLD, SIG_IGN);

	slog_debug_l("6");

	/* Fork off for the second time*/
	pid = fork();

	slog_debug_l("7");

	/* An error occurred */
	if (pid < 0) {
		slog_debug_l("A fork error");
		exit(EXIT_FAILURE);
	}

	slog_debug_l("8");

	/* Success: Let the parent terminate */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	slog_debug_l("9");

	/* Set new file permissions */
	umask(0);

	slog_debug_l("10");

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	chdir("/tmp/");

	slog_debug_l("11");

	/* Close all open file descriptors */
	for (fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--) {
		close(fd);
	}

	/* Reopen stdin (fd = 0), stdout (fd = 1), stderr (fd = 2) */
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w+");
	stderr = fopen("/dev/null", "w+");

	/* Try to write PID of daemon to lockfile */
	if (core->pid_file_name != NULL) {
		char str[256];
		core->pid_fd = open(core->pid_file_name, O_RDWR | O_CREAT, 0640);
		if (core->pid_fd < 0) {
			/* Can't open lockfile */
			exit(EXIT_FAILURE);
		}
		if (lockf(core->pid_fd, F_TLOCK, 0) < 0) {
			/* Can't lock file */
			exit(EXIT_FAILURE);
		}
		/* Get current PID */
		sprintf(str, "%d\n", getpid());
		/* Write PID to lockfile */
		write(core->pid_fd, str, strlen(str));
	}
}

