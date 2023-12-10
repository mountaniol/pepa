#include "pepa_core.h"

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to FAIL state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int tarns_to_state_fail(void)
{
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transit to disconnected state
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int tarns_to_state_disconnected(void)
{
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to CONNECTING state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int tarns_to_state_connecting(void)
{
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to ESTABLISHED state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int tarns_to_state_established(void)
{
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to OPERATING state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int tarns_to_state_operating(void)
{
	return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (12/8/23)
 * @brief Transition to COLLAPSE state: close all sockets
 * @param  void  
 * @return int PEPA_ERR_OK on success
 * @details 
 */
static int tarns_to_state_collapse(void)
{
	return PEPA_ERR_OK;
}

int pepa_change_state(pepa_state_t new_state)
{
	switch (new_state) {
	case PEPA_ST_FAIL:
		return tarns_to_state_fail();
	case PEPA_ST_DISCONNECTED:
		return tarns_to_state_disconnected();
	case PEPA_ST_CONNECTING:
		return tarns_to_state_connecting();
	case PEPA_ST_ESTABLISHED:
		return tarns_to_state_established();
	case PEPA_ST_OPERATING:
		return tarns_to_state_operating();
	case PEPA_ST_COLLAPSE:
		return tarns_to_state_collapse();
	}

	/* Shold never be here */
	return -1;
}

void *pepa_state_machine_thread(void *arg)
{
	int rc; 
	pepa_core_t *core = pepa_get_core();
	while (1) {

		switch (core->state) {
	case PEPA_ST_FAIL:
		rc = pepa_change_state(PEPA_ST_DISCONNECTED);
	case PEPA_ST_DISCONNECTED:
		rc =  pepa_change_state(PEPA_ST_CONNECTING);
	case PEPA_ST_CONNECTING:
		rc = pepa_change_state(PEPA_ST_CONNECTING);
	case PEPA_ST_ESTABLISHED:
		return tarns_to_state_established();
	case PEPA_ST_OPERATING:
		return tarns_to_state_operating();
	case PEPA_ST_COLLAPSE:
		return tarns_to_state_collapse();
		}
	} /* while (1) */

}

