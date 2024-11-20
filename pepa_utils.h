#ifndef PEPA_UTILS_H__
#define PEPA_UTILS_H__
#include "pepa_core.h"
#include "pepa_errors.h"

/**
 * @define
 * This define returns True if there is EPOLLIN in event 
 */
#define EVENT_HAS_BUFFER(_Fd) (events & EPOLLIN)

/**
 * @define
 * This define returns True if there is NO EPOLLIN in event 
 */
#define EVENT_NO_BUFFER(_Fd) (EPOLLIN != (events & EPOLLIN))

char *pepa_detect_socket_name_by_fd(const pepa_core_t *core, const int fd)  __attribute__((warn_unused_result));
void pepa_set_fd_operations(pepa_core_t *core, const int fd, const int op);
pepa_bool_t pepa_util_is_socket_valid(int sock)  __attribute__((warn_unused_result));
int set_socket_blocking_mode(int sockfd)  __attribute__((warn_unused_result));
pepa_bool_t utils_is_socket_blocking(int socket_fd)  __attribute__((warn_unused_result));
const char *utils_socked_blocking_or_not(int socket_fd)  __attribute__((warn_unused_result));
int bytes_available_read(int sockfd)  __attribute__((warn_unused_result));
int bytes_available_write(int sockfd)  __attribute__((warn_unused_result));
int bytes_size_of_send_queue(int sockfd)  __attribute__((warn_unused_result));

ssize_t send_exact_flags(const int sock_fd, const void *buf, const size_t num_bytes, int flags)  __attribute__((warn_unused_result));
ssize_t send_exact(const int sock_fd, const void *buf, const size_t num_bytes)  __attribute__((warn_unused_result));
ssize_t send_exact_more(const int sock_fd, const void *buf, const size_t num_bytes)  __attribute__((warn_unused_result));
ssize_t send_exact_to_file(const int sock_fd, const char *buf, const size_t num_bytes, const char *name, const int instance)  __attribute__((warn_unused_result));
ssize_t recv_exact(const int sock_fd, char *buf, const size_t buf_size, const size_t num_bytes, const char *caller, const int line)  __attribute__((warn_unused_result));
int check_socket_status(int sockfd);
const char *pepa_dump_event(const int ev);

#endif /* PEPA_UTILS_H__ */
