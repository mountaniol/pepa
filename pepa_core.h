#ifndef _PEPA_CORE_H_
#define _PEPA_CORE_H_

#include <semaphore.h>
#include <stdint.h>

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
 * @author Sebastian Mountaniol (7/20/23)
 * @brief THis structure unites all variables and file
 *  	  descriptors. It configured on the start and then
 *  	  passed to all threads.
 * @details 
 */
typedef struct {
	sem_t mutex; /**< A semaphor used to sync the struct between multiple threads */
	int  fd_out; /**< File descriptor of IN socket, i.e., a socket to read from */
	int  fd_in; /**< File descriptor of OUT socket, i.e., a socket write to*/
	int  fd_shva; /**< File descriptor of the SHVA opened socket, i.e. external server */
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
int pepa_lock_core(void);

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Unlock core
 * @return int 0 on success
 * @details This function should never fail.
 * @todo Should it be void?
 */
int pepa_unlock_core(void);

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Set new value to SHVA file descriptor
 * @param int fd    New SHVA file descriptor to set
 * @details This operation does not lock the core. The core MUST
 *  		be locked when this function is called.
 */
void pepa_core_set_shva_fd(int fd);

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Set new value of the OUT file descriptor
 * @param int fd    New OUT file descriptor to set
 * @details This operation does not lock the core. The core must
 *  		be locked when this function s called.
 */
void pepa_core_set_out_fd(int fd);


/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Get SHVA file descriptor
 * @return int File descriptor
 * @details This operation does not lock the core. The core MUST
 *  		be locked all time the file descriptor in use. If fd
 *  		< 0, it means the socked is closed
 */
int pepa_core_get_shva_fd(void);

/**
 * @author Sebastian Mountaniol (12/6/23)
 * @brief Get value of the OUT file descriptor
 * @return int File descriptor
 * @details This operation does not lock the core. The core MUST
 *  		be locked all time the file descriptor in use. If fd
 *  		< 0, it means the socked is closed
 */
int pepa_core_get_out_fd(void);

#endif /* _PEPA_CORE_H_ */
