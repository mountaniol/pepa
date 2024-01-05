#ifndef _PEPA_CORE_H_
#define _PEPA_CORE_H_

#include <arpa/inet.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdint.h>

#include "buf_t/buf_t.h"

/**
* @author Sebastian Mountaniol (12/12/23)
* @brief Per-thread variables
* @details 
*/
typedef struct {
	pthread_t thread_id; /**< UD of thread */
	buf_t *ip_string; /**< IP of socket  */
	int32_t port_int; /**< Port of socket  */
	uint32_t clients; /**< Number of clients on this socket */
} thread_vars_t;

/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Sockets (listen / read / write ) of all threads are
 *  	  gathered in this stucture
 * @details 
 */
typedef struct {
	int shva_rw;
	sem_t shva_rw_mutex;
	int out_listen;
	int out_write;
	int in_listen;
} pepa_sockets_t;

typedef enum {
	PEPA_TH_OUT_START = 0, /* Start thread routines */
	PEPA_TH_OUT_CREATE_LISTEN, /* Create listening socket */
	PEPA_TH_OUT_ACCEPT, /* Run accept() which creates Write socket */
	PEPA_TH_OUT_WATCH_WRITE_SOCK, /* Watch the status of Write  socket */
	PEPA_TH_OUT_CLOSE_WRITE_SOCKET, /* Close Write socket */
	PEPA_TH_OUT_CLOSE_LISTEN_SOCKET, /* Close listening socket */
	PEPA_TH_OUT_TERMINATE /* Terminate thread */
} pepa_out_thread_state_t;

typedef enum {
	PEPA_TH_SHVA_START = 0, /* Start thread routines */
	PEPA_TH_SHVA_OPEN_CONNECTION,
	PEPA_TH_SHVA_WAIT_OUT, /* Wait until OUT thread is connected */
	PEPA_TH_SHVA_START_TRANSFER, /* Start transfering thread */
	PEPA_TH_SHVA_WATCH_SOCKET, /* Watch the status of SHAV serrver socket */
	PEPA_TH_SHVA_CLOSE_SOCKET, /* Close connection to SHVA sserver */
	PEPA_TH_SHVA_TERMINATE /* Terminate thread */
} pepa_shva_thread_state_t;

typedef enum {
	PEPA_TH_IN_START = 0, /* Start thread routines */
	PEPA_TH_IN_CREATE_LISTEN, /* Create listening socket */
	PEPA_TH_IN_CLOSE_LISTEN, /* Create listening socket */
	PEPA_TH_IN_TEST_LISTEN_SOCKET, /* Create listening socket */
	PEPA_TH_IN_WAIT_SHVA_UP, /* Wait until SHVA is connected  */
	PEPA_TH_IN_WAIT_SHVA_DOWN, /* Wait until SHVA is connected  */
	PEPA_TH_IN_START_TRANSFER, /* Start transfering thread */
	PEPA_TH_IN_TERMINATE /* Terminate thread */
} pepa_in_thread_state_t;

typedef enum {
	PEPA_ST_NONE = 0,
	PEPA_ST_RUN,
	PEPA_ST_FAIL,
	PEPA_ST_SOCKET_RESET,
	PEPA_ST_MAX
} pepa_sig_t;

typedef enum {
	PEPA_PR_OUT = 0,
	PEPA_PR_SHVA, /* Only SHVA writes to this register */
	PEPA_PR_IN, /* Only PEPA_PR_IN writes to this register */
	PEPA_PR_MAX
} pepa_proc_t;

typedef struct {
	pepa_sig_t signals[PEPA_PR_MAX];
	pthread_mutex_t signals_sem;
	pthread_cond_t sync;
	pthread_mutex_t sync_sem;
} pepa_status_t;

typedef struct {
	int onoff;
	uint64_t shva_rx;
	uint64_t shva_tx;
	uint64_t out_rx;
	uint64_t out_tx;
	uint64_t in_rx;
	uint64_t in_tx;
} pepa_stat_t;

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief This structure unites all variables and file
 *  	  descriptors. It configured on the start and then
 *  	  passed to all threads.
 * @details 
 */
typedef struct {
	sem_t mutex; /**< A semaphor used to sync the core struct between multiple threads */

	int slog_flags; /* Keep slog logger verbosity level*/
	char *slog_file; /* Keep slog output file name; if given, log will be saved there */
	char *slog_dir; /* Keep slog output file in this directory */
	int slog_print; /* Show slog output on terminal */

	thread_vars_t shva_thread; /**< Configuration of SHVA thread */
	thread_vars_t shva_forwarder; /**< Configuration of SHVA thread */
	thread_vars_t in_thread; /**< Configuration of IN thread */
	thread_vars_t in_forwarder; /**< Configuration of IN thread */
	thread_vars_t out_thread; /**< Configuration of OUT thread */
	thread_vars_t monitor_thread; /**< Configuration of OUT thread */
	uint32_t internal_buf_size; /**< Size of buffer used to pass packages, by defaiult COPY_BUF_SIZE bytes, see pepa_config.h */
	int32_t abort_flag; /**< Abort flag, if enabled, PEPA file abort on errors; for debug only */
	pepa_sockets_t sockets;
	int32_t monitor_timeout; /**< How many microseconds to sleep between tests in microseconds */
	pepa_status_t state;
	pepa_stat_t monitor;
	int32_t daemon; /* If not 0, start as a daemon */
	int pid_fd; /* File descriptor of PID file */
	char *pid_file_name; /* File name of PID file  */
} pepa_core_t;

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Init pepas core structure
 * @return int32_t 0 on success, a negative error code on
 *  	   failure.
 * @details This function should never fail. THe only reason it
 *  		can fail is lack of memory which means the OS in a
 *  		very stressed condition. The app must abort int this
 *  		case.
 */
int32_t pepa_core_init(void);

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Destroy core structure
 * @param  void  
 * @return int32_t 0 on success
 * @details 
 */
int32_t pepa_core_finish(void);

/* Get pointer to codre */
/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Get pointer to core
 * @return pepa_core_t* Pointer to core, never NULL
 * @details If a caller try to get the pointer and the core is NULL, the app will be aborted.
 * It is safe to hold core pointer without lock,
 * the core structure must as long as the app is running.
 */
__attribute__((hot))
pepa_core_t *pepa_get_core(void);

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Lock core; anyone else can manipulate locked values
 *  	  until it unlocked
 * @return int32_t 0 on success.
 * @details This function should never show failure.
 *  		It will wait on semaphore until the sem taken.
 * @todo Should it be void?
 */
int32_t pepa_core_lock(void);

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Unlock core
 * @return int32_t 0 on success
 * @details This function should never fail.
 * @todo Should it be void?
 */
int32_t pepa_core_unlock(void);

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Return abort flag; if core is not inited yet,
 *  	  always return NO (0)
 * @param  void  
 * @return int32_t Abort flag
 * @details 
 */
int32_t pepa_if_abort(void);


const char *pepa_out_thread_state_str(pepa_out_thread_state_t s);
const char *pepa_shva_thread_state_str(pepa_shva_thread_state_t s);
const char *pepa_in_thread_state_str(pepa_in_thread_state_t s);

void pepa_shva_socket_lock(pepa_core_t *core);
void pepa_shva_socket_unlock(pepa_core_t *core);
#endif /* _PEPA_CORE_H_ */
