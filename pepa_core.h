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
enum in_and_out_mode {
	MODE_FIFO = 17,
	MODE_FILE = 18,
	MODE_SOCKET_TCP = 19,
	MODE_SOCKET_UDP = 20,
	MODE_SOCKET_LOCAL = 21
};

typedef enum in_and_out_mode in_out_mode_t;

/**
 * @author Sebastian Mountaniol (7/20/23)
 * @brief THis structure unites all variables and file
 *  	  descriptors. It configured on the start and then
 *  	  passed to all threads.
 * @details 
 */
typedef struct {
	sem_t mutex; /**< A semaphor used to sync the struct between multiple threads */
	in_out_mode_t in_mode; /**< The mode of IN stream (which sended to the external socket)  */
	in_out_mode_t out_mode; /**< The mode of OUT stream (which accepts data from the external socket) */
	int  fd_out; /**< File descriptor of IN file, i.e., a file to read from */
	int  fd_in; /**< File descriptor of OUT file, i.e., a file write to*/
	int  fd_sock; /**< File descriptor on an opened socket, i.e. external server */
	char *file_name_fifo; /**< Filenname of the FIFO IN */
} pepa_core_t;

#endif /* _PEPA_CORE_H_ */
