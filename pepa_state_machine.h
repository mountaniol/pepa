#ifndef _PEPA_STATE_MACHINE_H_
#define _PEPA_STATE_MACHINE_H_

/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief This is implementation of state machine.
 * @param in new_state
 * @return int PEPA_ERR_OK in case of success
 * @details This function makes actions according to state.
 *  		It opens / closes sokets, starts/stops threads.
 */
int pepa_change_state(int new_state);

/**
 * @author Sebastian Mountaniol (12/11/23)
 * @brief Finish all thread but SHVA; Close all sockets.
 * @details This function returns everything to to very beginning state.
 * It closed all opened sockets, stop all threads but SHVA thread.
 * After this funcion is finished, the SHVA can start from the very beginning,
 * opening all sockets, starting all threads and so on.
 */
//void pepa_back_to_disconnected_state(void);

void pepa_back_to_disconnected_state_new(void);

void pepa_thread_kill_shva(void);
void pepa_thread_kill_out(void);
void pepa_thread_kill_in(void);

void pepa_thread_start_shva(void);
void pepa_thread_start_in(void);

int pepa_thread_is_shva_up(void);
int pepa_thread_is_in_up(void);
int pepa_thread_is_out_up(void);

void pepa_thread_start_ctl(void);
void pepa_thread_start_out(void);

void pepa_state_set(pepa_core_t *core, int process, int state, const char *func, const int line);
int pepa_state_get(pepa_core_t *core, int process);
void pepa_state_clear(pepa_core_t *core, int process);
void pepa_state_sig(pepa_core_t *core);
void pepa_state_wait(pepa_core_t *core);
void pepa_state_unlock(pepa_core_t *core);
void pepa_state_lock(pepa_core_t *core);
void pepa_state_clear(pepa_core_t *core, int process);
int pepa_state_to_action(int process, int state);

const char *pepa_pr_str(pepa_proc_t p);
const char *pepa_sig_str(pepa_sig_t p);
const char *pepa_act_str(pepa_action_t p);

#endif /* _PEPA_STATE_MACHINE_H_ */
