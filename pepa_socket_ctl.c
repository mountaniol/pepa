#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h> /* For read() */
#include <sys/eventfd.h> /* For eventfd */
/* THe next two are needed for send() */
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/epoll.h>

#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_socket_ctl.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_debug.h"
#include "pepa_state_machine.h"
#include "buf_t/buf_t.h"
#include "buf_t/se_debug.h"

static int pepa_ctl_execute_action(pepa_action_t act)
{
	DDD("EXECUTING ACTION: %s\n", pepa_act_str(act));
	switch (act) {
	case PEPA_ACT_NONE:
		DDD("CTL: Run action PEPA_ACT_NONE\n");
		return 0;
	case PEPA_ACT_START_OUT:
		DDD("CTL: Run action PEPA_ACT_START_OUT\n");
		pepa_thread_start_out();
		return 0;
	case PEPA_ACT_START_IN:
		DDD("CTL: Run action PEPA_ACT_START_IN\n");
		pepa_thread_start_in();
		return 0;
	case PEPA_ACT_START_SHVA:
		DDD("CTL: Run action PEPA_ACT_START_SHVA\n");
		pepa_thread_start_shva();
		return 0;
	case PEPA_ACT_STOP_OUT:
		DDD("CTL: Run action PEPA_ACT_STOP_OUT\n");
		pepa_thread_kill_out();
		return 0;
	case PEPA_ACT_STOP_IN:
		DDD("CTL: Run action PEPA_ACT_STOP_IN\n");
		pepa_thread_kill_in();
		return 0;
	case PEPA_ACT_STOP_SHVA:
		DDD("CTL: Run action PEPA_ACT_STOP_SHVA\n");
		pepa_thread_kill_shva();
		return 0;
	case PEPA_ACT_RESTART_ALL:
		DDD("CTL: Run action PEPA_ACT_RESTART_ALL\n");
		pepa_back_to_disconnected_state_new();
		sleep(1);
		pepa_thread_start_out();
		return 0;
	case PEPA_ACT_ABORT:
		DDD("CTL: Run action PEPA_ACT_ABORT\n");
		abort();
	case PEPA_ACT_MAX:
		DDD("CTL: Should never be here!\n");
		abort();
	default:
		DDD("CTL: Should never be here!\n");
		abort();
	}
	DDD("CTL: Should never be here!\n");
	return PEPA_ERR_OK;
}

void *pepa_ctl_thread_new(__attribute__((unused))void *arg)
{
	int         rc    = 0;
	pepa_core_t *core = pepa_get_core();

	rc = pepa_pthread_init_phase("CTL");
	if (rc < 0) {
		DE("Could not init CTL\n");
		pthread_exit(NULL);
	}
	/* This is the main loop of this thread */

	while (1) {
		pepa_proc_t   process_num;
		pepa_action_t act;
		pepa_state_wait(core);

		DDD("CTL_GOT_SIGNAL\n");

		for (process_num = 0; process_num < PEPA_PR_MAX; process_num++) {
			int st = 0;
			act = PEPA_ACT_NONE;
			st = pepa_state_get(core, process_num);
			if (PEPA_ST_NONE != st) {
				act = pepa_state_to_action(process_num, st);
			}
			if (PEPA_ACT_NONE != act) {
				DDD("Finished the signal processing: proc %s, sig %s, act %d\n",
				   pepa_pr_str(process_num),
				   pepa_sig_str(st), act);
				pepa_ctl_execute_action(act);
				pepa_state_clear(core, process_num);
				break;
			}
		}
	}
}



