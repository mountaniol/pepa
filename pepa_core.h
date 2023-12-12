#ifndef _PEPA_CORE_H_
#define _PEPA_CORE_H_

#include <arpa/inet.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdint.h>

#include "buf_t/buf_t.h"

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief This enum used to indicate what is the mode of IN and
 *  	  OUT streams. A stream can be file, fifo or socket.
 *  		The socket can be be one of 3 types: TCP, UDP ot
 *  		LOCAL (an Unux Domain Socket)
 * @details 
 */
typedef enum in_and_out_mode {
	MODE_NOT_INITED = 11,
	MODE_FIFO = 17,
	MODE_FILE = 18,
	MODE_SOCKET_TCP = 19,
	MODE_SOCKET_UDP = 20,
	MODE_SOCKET_LOCAL = 21
} in_out_mode_t;

/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief These PEPA_ST_* values describe the state machine
 *  	  states of PEPA-NG. 
 * @details Here is the explanation of each state, signals of
 *          transition from state to state and actions:
 *
 * PEPA_ST_DISCONNECTED:
 * This state is the initial state. All sockets are down.
 * The PEPA-NG waits until the OUT socket is up, which means,
 * a remote client is connected. When it happens, PEPA makes
 * transition to the next state, CONNECTING
 *
 * PEPA_ST_CONNECTING:
 * In this state, PEPA starts a connection to the SHVA server.
 * When the connection is established, PEPA makes the transition to the
 * state ESTABLISHED.
 * If the connection to SHVA can not be established, PEPA closes the
 * OUT connection, and make the transition to the DISCONNECTED
 * state.
 *
 * PEPA_ST_ESTABLISHED:
 * In this state, PEPA is waiting for an IN connection.
 * It also passes all packets from SHVA to the OUT socket.
 * When the first IN connection is established, it makes the transition
 * to OPERATING state.
 *
 * In case of OUT or SHVA socket, it closes all sockets and makes
 * transition to DISCONNECTED state.
 *
 * PEPA_ST_OPERATING:
 * This is the main state where PEPA should be all the time.
 * All connections are established.
 * Packets from SHVA are being transferred to OUT.
 * Packets from multiple IN sockets are being transferred to SHVA.
 *
 * All IN connection could be closed; in this case PEPA makes
 * transition to ESTABLISHED.
 *
 * If any of the SHVA or OUT sockets is closed, PEPA closes another
 * socket (either SHVA or OUT, which of them is still opened)
 * and made the transition to an INTERMEDIATE state.
 *
 * PEPA_ST_COLLAPSE: This is the state when there is no
 * connection to SHVA and OUT, but IN sockets are still alive.
 * In this state, PEPS closes all IN sockets and makes a
 * transition to the DISCONNECTED state.
 */
typedef enum {
	PEPA_ST_FAIL = 1,
	PEPA_ST_DISCONNECTED, /**< Initial state: all sockets are down */
	PEPA_ST_CONNECTING, /**< OUT connection established */
	PEPA_ST_ESTABLISHED, /**< SHVA connection established */
	PEPA_ST_OPERATING, /**< IN connection stablished */
	PEPA_ST_COLLAPSE /**< SHVA failed; IN connections are still established */
} pepa_state_t;

typedef enum {
	PEPA_SIG_OUT_CLOSED = 1, /**< OUT socket is closed */
	PEPA_SIG_SHVA_CLOSED, /**< SHVA socket is closed */
	PEPA_SIG_ALL_IN_CLOSED, /**< All INsockets are closed */
	PEPA_SIG_OUT_CONNECTED, /**< OUT socket connected */
	PEPA_SIG_SHVA_CONNECTED, /**< SHVA connection is established */
	PEPA_SIG_ALL_IN_OPENED, /**< At least one IN socket is connected */
	PEPA_SIG_FAIL, /**< Could not execute a critical operation, like opening socket; state machine should transit to FAIL */
}
pepa_state_sig_t;

typedef struct {
	int event_fd; /**< This is a socket to signal between IN and Acceptro threads */
	buf_t * buf_fds; /**< Array of sockets file descriptors; filled in Acceptor thread, then merged in IN thread */
	sem_t buf_fds_mutex; /**< A semaphor used to sync the core struct between multiple threads */
	struct sockaddr_in   s_addr;
	int socket; /** < Socket to accept new connections */
} pepa_in_thread_fds_t;

/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Per-thread variables
 * @details 
 */
typedef struct {
	pthread_t thread_id; /**< UD of thread */
	buf_t *ip_string; /**< IP of socket  */
	int port_int; /**< Port of socket  */
	int clients; /**< Number of clients on this socket */
} thread_vars_t;

/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Sockets (listen / read / write ) of all threads are
 *  	  gathered in this stucture
 * @details 
 */
typedef struct {
	int shva_rw;
	int out_listen;
	int out_read;
	int in_listen;
} pepa_sockets_t;

/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Event sockets for Control thread
 * @details 
 */
typedef struct {
	int ctl_from_shva; /**< This event fd will signalize from SHVA to CONTROL */
	int ctl_from_in; /**< This event fd will signalize that IN thread is canceled */
	int ctl_from_out; /**< This event fd will signalize that OUT thread is canceled */
	int ctl_from_acceptor; /**< This event fd will signalize that ACCEPTOR thread is canceled */

	int shva_from_ctl; /**< This event fd listened in SHVA thread and receive commands from CONTROL thread */
	int in_from_ctl; /**< This event fd listened in SHVA thread and receive commands from CONTROL thread */
	int out_from_ctl; /**< This event fd listened in SHVA thread and receive commands from CONTROL thread */
	int acceptor_from_ctl; /**< This event fd listened in SHVA thread and receive commands from CONTROL thread */
} pepa_control_fds_t;

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief This structure unites all variables and file
 *  	  descriptors. It configured on the start and then
 *  	  passed to all threads.
 * @details 
 */
typedef struct {
	sem_t mutex; /**< A semaphor used to sync the core struct between multiple threads */

	int state; /**< This is state machine status */

	thread_vars_t ctl_thread; /**< Configuration of SHVA thread */
	thread_vars_t shva_thread; /**< Configuration of SHVA thread */
	thread_vars_t in_thread; /**< Configuration of IN thread */
	thread_vars_t out_thread; /**< Configuration of OUT thread */
	thread_vars_t acceptor_thread; /**< Configuration of OUT thread */
	int internal_buf_size; /**< Size of buffer used to pass packages, by defaiult COPY_BUF_SIZE bytes, see pepa_config.h */
	int abort_flag; /**< Abort flag, if enabled, PEPA file abort on errors; for debug only */
	buf_t *buf_in_fds; /* IN thread file descriptors of opened connections (read sockets) */
	pepa_in_thread_fds_t *acceptor_shared; /* The structure shared between IN and Acceptor thread */
	pepa_control_fds_t controls; /**< These event fds used for communication between control thread and all other threads */
	pepa_sockets_t sockets;
} pepa_core_t;

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Init pepas core structure
 * @return int 0 on success, a negative error code on failure.
 * @details This function should never fail. THe only reason it
 *  		can fail is lack of memory which means the OS in a
 *  		very stressed condition. The app must abort int this
 *  		case.
 */
int pepa_core_init(void);

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Destroy core structure
 * @param  void  
 * @return int 0 on success
 * @details 
 */
int pepa_core_finish(void);

/* Get pointer to codre */
/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Get pointer to core
 * @return pepa_core_t* Pointer to core, never NULL
 * @details If a caller try to get the pointer and the core is NULL, the app will be aborted.
 * It is safe to hold core pointer without lock,
 * the core structure must as long as the app is running.
 */
pepa_core_t *pepa_get_core(void);

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Lock core; anyone else can manipulate locked values
 *  	  until it unlocked
 * @return int 0 on success.
 * @details This function should never show failure.
 *  		It will wait on semaphore until the sem taken.
 * @todo Should it be void?
 */
int pepa_core_lock(void);

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Unlock core
 * @return int 0 on success
 * @details This function should never fail.
 * @todo Should it be void?
 */
int pepa_core_unlock(void);


/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief Set new state
 * @param int state State to set
 */
void pepa_set_state(int state);

/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief Get current state
 * @return int Current state
 */
int pepa_get_state(void);

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Return abort flag; if core is not inited yet,
 *  	  always return NO (0)
 * @param  void  
 * @return int Abort flag
 * @details 
 */
int pepa_if_abort(void);
#endif /* _PEPA_CORE_H_ */
