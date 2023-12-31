#ifndef _PEPA_SOCKET_H__
#define _PEPA_SOCKET_H__

#include <semaphore.h>
#include <pthread.h>

#include "buf_t/buf_t.h"
#include "pepa_core.h"

/* Arguments for thread clean hook  */
typedef struct {
	int epoll_fd;
	char *buf;
} thread_clean_args_t;

void set_sig_handler(void);

__attribute__((warn_unused_result))
int pepa_pthread_init_phase(const char *name);
void pepa_parse_pthread_create_error(const int rc);

void pepa_set_tcp_timeout(pepa_core_t *core, int sock);
void pepa_set_tcp_recv_size(pepa_core_t *core, int sock);
void pepa_set_tcp_send_size(pepa_core_t *core, int sock);
void pepa_set_tcp_connection_props(pepa_core_t *core, int sock);

//int pepa_open_connection_to_server(pepa_core_t *core, const char *address, int port, const char *name);

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
int pepa_open_listening_socket(pepa_core_t *core, 
							   struct sockaddr_in *s_addr,
							   const buf_t *ip_address,
							   const int port,
							   const int num_of_clients,
							   const char *caller_name);


__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Open socket to remote address:port
 * @param const char* address IP address as a string
 * @param int port   Port
 * @return int Opened socket >= 0, negative core on an error
 */
int pepa_open_connection_to_server(pepa_core_t *core, const char *address, int port, const char *name);

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
