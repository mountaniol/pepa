#ifndef _PEPA_SOCKET_H__
#define _PEPA_SOCKET_H__

#include <semaphore.h>
#include <pthread.h>

#include "buf_t/buf_t.h"
#include "pepa_core.h"
#include "pepa_ticket_id.h"

void set_sig_handler(void);

/* This enum controld whether PEPA adding PEPA ID and / or PEPA ticket to buffers passing from SHVA to OUT */
enum {
    PEPA_DINABLE_TICKETS_AND_ID,
    PEPA_ENABLE_TICKETS_AND_ID,
    PEPA_ENABLE_TICKETS,
    PEPA_ENABLE_ID
};

typedef struct {
    char *buf;
    size_t buf_room;
    size_t buf_used;
    pepa_prebuf_t prebuf;
    int send_prebuf;
    size_t prebuf_size;
} buf_and_header_t;

__attribute__((warn_unused_result))
int32_t pepa_pthread_init_phase(const char *name);
void pepa_parse_pthread_create_error(const int32_t rc);

void pepa_set_tcp_timeout(const int32_t sock);
void pepa_set_tcp_recv_size(const pepa_core_t *core, const int32_t sock, const char *name);
void pepa_set_tcp_send_size(const pepa_core_t *core, const int32_t sock, const char *name);

/**
 * @author Sebastian Mountaniol (20/10/2024)
 * @brief Receive a buffer frim one socket (file descriptor), and send it to another 
 * @param pepa_core_t* core          PEPA Core structure
 * @param const int fd_out           Socket File descriptor to write buffer(s)
 * @param const char* name_out       Name of File descriptor to write (for debug printings)
 * @param const int fd_in            Socket File descriptor to read buffer(s)
 * @param const char* name_in        Name of File descriptor to read (for debug printings)
 * @param char* buf                  Preallocated buffer to use for reading/writing
 * @param const size_t buf_size      Preallocated buffer size (in bytes)
 * @param const int do_debug         This flag enables extra debug prints
 * @param uint64_t* ext_rx           Pointer to statistic RX variable; updated when a buffer received 
 * @param uint64_t* ext_tx           Pointer to statistic TX variable; updated when a buffer received 
 * @param const int max_iterations   How many times it should read from RX wnd write to TX socket
 * @return int                       EOK if everything is OK, a negative error code if one of the sockets is
 *         degraded and should be closed: -PEPA_ERR_BAD_SOCKET_WRITE if the 'fd_out' socket is degraded,
 *         -PEPA_ERR_BAD_SOCKET_READ if RX socket is degraded
 * @details Several 'core' variables change the behavior of this function:
 * If core->dump_messages is not 0, the received buffer is printed to logger before it transfered
 * If core->use_ticket is not 0, and TX socket is OUT socket, a ticket will be added to the beginning of the
 * buffer
 * If core->use_id is not 0, the core->id_val will be added after the 'ticket' variable (or in the beginning
 * of the buffer, if 'tickets' are disabled)
 */
int pepa_one_direction_copy4(/* 1 */pepa_core_t *core,
                             /* 2 */ char *buf,
                             /* 3 */ const size_t buf_size,
                             /* 4 */ const int fd_out,
                             /* 5 */ const char *name_out,
                             /* 6 */ const int fd_in,
                             /* 7 */ const char *name_in,
                             /* 8 */ const int do_debug,
                             /* 9 */ uint64_t *ext_rx,
                             /* 10 */ uint64_t *ext_tx,
                             /* 11 */ const int max_iterations);

int pepa_one_direction_copy3(pepa_core_t *core,
                             const int fd_out, const char *name_out,
                             const int fd_in, const char *name_in,
                             const int do_debug,
                             uint64_t *ext_rx, uint64_t *ext_tx,
                             const int max_iterations);


int transfer_data4(pepa_core_t *core,
                   const int fd_out, const char *name_out,
                   const int fd_in, const char *name_in,
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
int32_t pepa_test_fd(const int fd);


__attribute__((warn_unused_result))
int32_t epoll_ctl_add(const int epfd, const int fd, const uint32_t events);

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
                                   const uint16_t port,
                                   const int32_t num_of_clients,
                                   const char *caller_name);


__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/12/23)
 * @brief Open socket to remote address:port
 * @param const char* address IP address as a string
 * @param uint16_t port   Port
 * @return int32_t Opened socket >= 0, negative core on an error
 */
int32_t pepa_open_connection_to_server(const char *address, const uint16_t port, const char *name);

__attribute__((warn_unused_result))
int32_t pepa_socket_shutdown_and_close(const int sock, const char *my_name);

void pepa_socket_close(const int fd, const char *socket_name);

void pepa_socket_close_in_listen(pepa_core_t *core);

int pepa_find_socket_port(const int sock);
void pepa_reading_socket_close(const int fd, const char *socket_name);


int pepa_disconnect_shva(pepa_core_t *core);
int pepa_disconnect_in_rw(pepa_core_t *core);
int pepa_disconnect_in_listen(pepa_core_t *core);
int pepa_disconnect_out_rw(pepa_core_t *core);
int pepa_disconnect_out_listen(pepa_core_t *core);
int pepa_disconnect_all_sockets(pepa_core_t *core);

//void pepa_print_pthread_create_error(const int32_t rc);
#endif /* _PEPA_SOCKET_H__ */
