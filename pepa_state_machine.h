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
void pepa_back_to_disconnected_state(void);


#endif /* _PEPA_STATE_MACHINE_H_ */
