#ifndef _PEPA_SOCKET_H__
#define _PEPA_SOCKET_H__

#include <semaphore.h>
#include <pthread.h>

#include "buf_t/buf_t.h"

/* Size of the xross buffer (for reading from / writing to cross socket */
#define X_BUF_SIZE (1024)

typedef struct {
	int fd_src;
	int fd_dst;
	buf_t *buf;
} x_connect_t;

typedef enum {
	X_CONN_COPY_LEFT = 1,
	X_CONN_COPY_RIGHT
} x_conn_direction_t;

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Start threads / state machine 
 * @return int PEPA_ERR_OK on success, a negative value on an
 *  	   error
 */
int pepa_start_threads(void);

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Stop threads, close all file descriptors et cetera;
 *  	  not implemented for today 
 * @param  void  
 * @return int PEPA_ERR_OK on success, a negative value on an
 *  	   error
 * @details 
 */
int pepa_stop_threads(void);
#endif /* _PEPA_SOCKET_H__ */
