#ifndef PEPA_UTILS_H__
#define PEPA_UTILS_H__
#include "pepa_core.h"

char *pepa_detect_socket_name_by_fd(const pepa_core_t *core, const int fd);
void pepa_set_fd_operations(pepa_core_t *core, const int fd, const int op);
int if_is_socket_valid(int sock);
int set_socket_blocking_mode(int sockfd);
int utils_socked_blocking_or_not_int(int socket_fd);
const char *utils_socked_blocking_or_not(int socket_fd);
int bytes_available_read(int sockfd);
int bytes_available_write(int sockfd);
int bytes_size_of_send_queue(int sockfd);

ssize_t send_exact_flags(const int sock_fd, const char *buf, const size_t num_bytes, int flags);
ssize_t send_exact(const int sock_fd, const char *buf, const size_t num_bytes);
ssize_t send_exact_more(const int sock_fd, const char *buf, const size_t num_bytes);
ssize_t send_exact_to_file(const int sock_fd, const char *buf, const size_t num_bytes, const char *name, const int instance);
ssize_t recv_exact(const int sock_fd, char *buf, const size_t buf_size, const size_t num_bytes, const char *caller, const int line);
int check_socket_status(int sockfd);

#endif /* PEPA_UTILS_H__ */
