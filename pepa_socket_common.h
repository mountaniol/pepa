#ifndef _PEPA_SOCKET_H__
#define _PEPA_SOCKET_H__

#include <semaphore.h>
#include <pthread.h>

#include "buf_t/buf_t.h"
#include "pepa_core.h"

typedef struct {
	char my_name[32];
	int fd_read; /**< Read from this fd */
	int fd_write; /**< Write to this fd */
	int fd_die; /**< If given, listen and exit when received event from parent thread */
	int close_read_sock; /**< If not 0, the thread will close reading socket before when termonated */
	int fd_eventpoll; /**< Eventpoll file descriptor, to be closed when the thread is finished */
	char *name;
	char *name_read;
	char *name_write;
	sem_t *fd_write_mutex;
	void *buf;
	uint64_t rx;
	uint64_t tx;
	uint64_t reads;
	uint64_t writes;

} pepa_fds_t;

#if 0 /* SEB */
typedef struct {
	int fd_src;
	int fd_dst;
	buf_t *buf;
}
x_connect_t;
#endif

#if 0 /* SEB */
typedef enum {
	X_CONN_COPY_LEFT = 1,
	X_CONN_COPY_RIGHT
}
x_conn_direction_t;
#endif

void set_sig_handler(void);

__attribute__((warn_unused_result))
int pepa_pthread_init_phase(const char *name);
void pepa_parse_pthread_create_error(const int rc);

__attribute__((warn_unused_result))
int pepa_open_connection_to_server(const char *address, int port, const char *name);

__attribute__((warn_unused_result))
int pepa_one_direction_copy2(int fd_out, const char *name_out,
							 int fd_in, const char *name_in,
							 char *buf, size_t buf_size, int do_debug, 
							 uint64_t *ext_rx, uint64_t *ext_tx);

/**
 * @author Sebastian Mountaniol (12/14/23)
 * @brief Test file descriptor. If it opened, return PEPA_ERR_OK,
 *  	  if it closed, returns -PEPA_ERR_FILE_DESCRIPTOR
 * @param int fd    
 * @return int 
 * @details 
 */
int pepa_test_fd(int fd);


__attribute__((warn_unused_result))
int epoll_ctl_add(int epfd, int fd, uint32_t events);

__attribute__((nonnull(1, 2)))
/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Open listening socket
 * @param struct sockaddr_in* s_addr Socket addr structure, will
 *  			 be fillled in the function
 * @param const buf_t* ip_address    buf_t containig IP address
 *  			as a string
 * @param const int port   IP port to listen       
 * @param const int num_of_clients Number of client this socket
 *  			will accept
 * @return int Socket file descriptor, >= 0, or a negative error
 *  	   code
 */
int pepa_open_listening_socket(struct sockaddr_in *s_addr, const buf_t *ip_address, const int port, const int num_of_clients, const char *caller_name);


__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Open socket to remote address:port
 * @param const char* address IP address as a string
 * @param int port   Port
 * @return int Opened socket >= 0, negative core on an error
 */
int pepa_open_connection_to_server(const char *address, int port, const char *name);

void *pepa_shva_thread_new(__attribute__((unused))void *arg);

void pepa_parse_pthread_create_error(const int rc);

void *pepa_in_thread(__attribute__((unused))void *arg);

void *pepa_out_thread(__attribute__((unused))void *arg);

__attribute__((warn_unused_result))
int pepa_socket_shutdown_and_close(int sock, const char *my_name);

void pepa_socket_close(int fd, const char *socket_name);

void pepa_socket_close_shva_rw(pepa_core_t *core);

void pepa_socket_close_out_write(pepa_core_t *core);

void pepa_socket_close_out_listen(pepa_core_t *core);

void pepa_socket_close_in_listen(pepa_core_t *core);

//void pepa_print_pthread_create_error(const int rc);
#endif /* _PEPA_SOCKET_H__ */
