#ifndef _PEPA_STATE_MACHINE_H_
#define _PEPA_STATE_MACHINE_H_

int pepa_change_state(int new_state);

void pepa_back_to_disconnected_state_new(void);

void pepa_thread_kill_shva(void);
void pepa_thread_kill_out(void);
void pepa_thread_kill_in(void);

void pepa_thread_start_shva(void);
void pepa_thread_start_in(void);

int pepa_thread_is_shva_up(void);
int pepa_thread_is_in_up(void);
int pepa_thread_is_out_up(void);

void pepa_thread_start_out(void);

int pepa_state_get(pepa_core_t *core, int process);
void pepa_state_sig(pepa_core_t *core);
void pepa_state_wait(pepa_core_t *core);

const char *pepa_pr_str(pepa_proc_t p);
const char *pepa_sig_str(pepa_sig_t p);
void pepa_kill_all_threads(void);

void pepa_state_shva_set(pepa_core_t *core, pepa_sig_t sig);
void pepa_state_in_set(pepa_core_t *core, pepa_sig_t sig);
void pepa_state_out_set(pepa_core_t *core, pepa_sig_t sig);
void pepa_state_ctl_set(pepa_core_t *core, pepa_sig_t sig);


int pepa_state_shva_get(pepa_core_t *core);
int pepa_state_in_get(pepa_core_t *core);
int pepa_state_out_get(pepa_core_t *core);
#endif /* _PEPA_STATE_MACHINE_H_ */
