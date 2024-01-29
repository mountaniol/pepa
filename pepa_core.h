#ifndef _PEPA_CORE_H_
#define _PEPA_CORE_H_

#include <arpa/inet.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdint.h>

#include "buf_t/buf_t.h"

/* Just a default mask */
#define CORE_VALIDITY_MASK ((uint32_t) 0xC04E0707)

/**
* @author Sebastian Mountaniol (12/12/23)
* @brief Per-thread variables
* @details These structure used both in Emulator and in pepa3;
*   	   in Emulator it really a thread, so we keep this name
*/
typedef struct {
	pthread_t thread_id; /**< UD of thread */
	buf_t *ip_string; /**< IP of socket  */
	uint16_t port_int; /**< Port of socket  */
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
	int out_listen;
	int out_write;
	int in_listen;
} pepa_sockets_t;

typedef struct {
	int onoff;
	uint64_t shva_rx;
	uint64_t shva_tx;
	uint64_t out_rx;
	uint64_t out_tx;
	uint64_t in_rx;
	uint64_t in_tx;
} pepa_stat_t;

typedef struct {
	int *sockets; /**< Array of IN reading sockets */
	int number; /**< Number of allocated slots (not number of active sockets!) */
} pepa_in_read_sockets_t;

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief This structure unites all variables and file
 *  	  descriptors. It configured on the start and then
 *  	  passed to all threads.
 * @details 
 */
typedef struct {
	uint32_t validity; /**< Should be inited with a VALIDITY mask */
	/* Logger configs */
	int slog_flags; /* Keep slog logger verbosity level*/
	char *slog_file; /* Keep slog output file name; if given, log will be saved there */
	char *slog_dir; /* Keep slog output file in this directory */
	int slog_print; /* Show slog output on terminal */
	int slog_color; /* Output on terminal should use color or not (by defailt: no) */
	int dump_messages; /* Output on terminal should use color or not (by defailt: no) */
	int monitor_divider; /* Divide monitor output (vytes) by this diveder */
	char monitor_divider_str[1]; /* Monitor divider as a string: B for bytess, K for kilobytes, M for megabytes */
	unsigned int emu_timeout; /* Timeout in microseconds between buffer sendings from emulator; defailt is 0 */
	size_t emu_max_buf; /* Max size of buffer of a buffer send from emulator */
	size_t emu_min_buf; /* Min size of buffer of a buffer send from emulator; defailt 1; must be > 0 */
	uint32_t emu_in_threads; /**< How many IN threads the Emulator should start */

	/* These thread_vars_t used in the Emulator */
	thread_vars_t shva_thread; /**< Configuration of SHVA thread */
	thread_vars_t in_thread; /**< Configuration of IN thread */
	thread_vars_t out_thread; /**< Configuration of OUT thread */
	thread_vars_t monitor_thread; /**< Configuration of OUT thread */


	unsigned int monitor_freq; /* How often (in seconds) print out statistics from the monitor; by default every 5 seconds */
	pepa_in_read_sockets_t in_reading_sockets; /**< Reading sockets opened when accept new connections */
	uint32_t internal_buf_size; /**< Size of buffer used to pass packages, by defaiult COPY_BUF_SIZE bytes, see pepa_config.h */
	int32_t abort_flag; /**< Abort flag, if enabled, PEPA file abort on errors; for debug only */
	pepa_sockets_t sockets;
	int32_t monitor_timeout; /**< How many microseconds to sleep between tests in microseconds */
	pepa_stat_t monitor;
	int32_t daemon; /* If not 0, start as a daemon */
	int pid_fd; /* File descriptor of PID file */
	char *pid_file_name; /* File name of PID file  */
	void *buffer;
	char *print_buf; /**< This buffer used in case message dump is on, to print out messages */
	uint32_t print_buf_len;
	int epoll_fd;
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

int pepa_core_release(pepa_core_t *core);

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

int pepa_core_is_valid(pepa_core_t *core);

#endif /* _PEPA_CORE_H_ */
