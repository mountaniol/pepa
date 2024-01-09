#ifndef _PEPA_STATE_MACHINE_H_
#define _PEPA_STATE_MACHINE_H_

void pepa_thread_cancel(pthread_t pid, const char *name);
void pepa_thread_start_monitor(pepa_core_t *core);
void pepa_thread_kill_monitor(pepa_core_t *core);

#endif /* _PEPA_STATE_MACHINE_H_ */
