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
int pepa_change_state(in new_state);


#endif /* _PEPA_STATE_MACHINE_H_ */
