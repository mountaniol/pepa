#ifndef _PEPA_SOCKET_H__
#define _PEPA_SOCKET_H__

#include <semaphore.h>
#include <pthread.h>

#include "buf_t/buf_t.h"
#include "pepa_core.h"

void set_sig_handler(void);

__attribute__((warn_unused_result))
int32_t pepa_pthread_init_phase(const char *name);
void pepa_parse_pthread_create_error(const int32_t rc);

void pepa_set_tcp_timeout(int32_t sock);
void pepa_set_tcp_recv_size(pepa_core_t *core, int32_t sock);
void pepa_set_tcp_send_size(pepa_core_t *core, int32_t sock);

int pepa_one_direction_copy3(pepa_core_t *core,
							 int fd_out, const char *name_out,
							 int fd_in, const char *name_in,
							 char *buf, const size_t buf_size,
							 const int do_debug,
							 uint64_t *ext_rx, uint64_t *ext_tx,
							 const int max_iterations);

/**
 * @author Sebastian Mountaniol (12/14/23)
 * @brief Test file descriptor. If it opened, return PEPA_ERR_OK,
 *  	  if it closed, returns -PEPA_ERR_FILE_DESCRIPTOR
 * @param int32_t fd    
 * @return int32_t 
 * @details 
 */
int32_t pepa_test_fd(int32_t fd);


__attribute__((warn_unused_result))
int32_t epoll_ctl_add(int32_t epfd, int32_t fd, uint32_t events);

__attribute__((nonnull(1, 2)))
/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Open listening socket
 * @param struct sockaddr_in* s_addr Socket addr structure, will
 *  			 be fillled in the function
 * @param const buf_t* ip_address    buf_t containig IP address
 *  			as a string
 * @param const int32_t port   IP port to listen       
 * @param const int32_t num_of_clients Number of client this socket
 *  			will accept
 * @return int32_t Socket file descriptor, >= 0, or a negative error
 *  	   code
 */
int32_t pepa_open_listening_socket(struct sockaddr_in *s_addr,
								   const buf_t *ip_address,
								   const int32_t port,
								   const int32_t num_of_clients,
								   const char *caller_name);


__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Open socket to remote address:port
 * @param const char* address IP address as a string
 * @param int32_t port   Port
 * @return int32_t Opened socket >= 0, negative core on an error
 */
int32_t pepa_open_connection_to_server(const char *address, int32_t port, const char *name);

void pepa_parse_pthread_create_error(const int32_t rc);

__attribute__((warn_unused_result))
int32_t pepa_socket_shutdown_and_close(int32_t sock, const char *my_name);

void pepa_socket_close(int32_t fd, const char *socket_name);

void pepa_socket_close_in_listen(pepa_core_t *core);

int pepa_find_socket_port(int sock);
void pepa_reading_socket_close(int fd, const char *socket_name);

//void pepa_print_pthread_create_error(const int32_t rc);
#endif /* _PEPA_SOCKET_H__ */
