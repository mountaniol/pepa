#ifndef _PEPA_STATE_MACHINE_H_
#define _PEPA_STATE_MACHINE_H_

void pepa_back_to_disconnected_state_new(pepa_core_t *core);

void pepa_thread_kill_shva_forwarder(pepa_core_t *core);
void pepa_thread_kill_shva(pepa_core_t *core);
void pepa_thread_kill_out(pepa_core_t *core);
void pepa_thread_kill_in(pepa_core_t *core);
void pepa_thread_kill_in_fw(pepa_core_t *core);
void pepa_thread_kill_monitor(pepa_core_t *core);

void pepa_thread_start_shva(pepa_core_t *core);
void pepa_thread_start_in(pepa_core_t *core);
void pepa_thread_start_in_fw(pepa_core_t *core);
void pepa_thread_start_monitor(pepa_core_t *core);

__attribute__((warn_unused_result))
int32_t pepa_thread_is_shva_up(pepa_core_t *core);
__attribute__((warn_unused_result))
int32_t pepa_thread_is_in_up(pepa_core_t *core);
__attribute__((warn_unused_result))
int32_t pepa_thread_is_out_up(pepa_core_t *core);

void pepa_thread_start_out(pepa_core_t *core);

void pepa_state_sig(pepa_core_t *core);
void pepa_state_wait(pepa_core_t *core);

__attribute__((warn_unused_result))
const char *pepa_sig_str(pepa_sig_t p);
void pepa_kill_all_threads(pepa_core_t *core);

void pepa_state_shva_set(pepa_core_t *core, pepa_sig_t sig);
void pepa_state_in_set(pepa_core_t *core, pepa_sig_t sig);
void pepa_state_out_set(pepa_core_t *core, pepa_sig_t sig);
//void pepa_state_in_fw_set(pepa_core_t *core, pepa_sig_t sig);

__attribute__((warn_unused_result))
int32_t pepa_state_shva_get(pepa_core_t *core);
__attribute__((warn_unused_result))
int32_t pepa_state_in_get(pepa_core_t *core);
__attribute__((warn_unused_result))
int32_t pepa_state_out_get(pepa_core_t *core);

__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Start threads / state machine 
 * @return int32_t PEPA_ERR_OK on success, a negative value on an
 *  	   error
 */
int32_t pepa_start_threads(pepa_core_t *core);

#endif /* _PEPA_STATE_MACHINE_H_ */
