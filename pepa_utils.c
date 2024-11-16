#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>

#include <sys/ioctl.h>
#include <error.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "pepa_utils.h"
#include "pepa_in_reading_sockets.h"
#include "slog/src/slog.h"

#define SEC_TO_USEC(x) (x * 100000)

/* How many second to wait on receive before give up? */
#define RECV_WAIT_SEC (10)

/* How many second to wait on send before give up? */
#define SEND_WAIT_SEC (10)

/* How many usecs to wait between tryes to read? */
#define SLEEP_BETWEEN_TRY_USEC (10000)

char *pepa_detect_socket_name_by_fd(const pepa_core_t *core, const int fd)
{
    if (fd < 0) {
        return "CLOSE SOCKET";
    }
    if (fd == core->sockets.in_listen) {
        return "IN LISTEN";
    }
    if (fd == core->sockets.out_listen) {
        return "OUT LISTEN";
    }
    if (fd == core->sockets.out_write) {
        return "OUT RW";
    }
    if (fd == core->sockets.shva_listen) {
        return "SHVA LISTEN";
    }
    if (fd == core->sockets.shva_rw) {
        return "SHVA RW";
    }

    if (FD_IS_IN == pepa_if_fd_in(core, fd)) {
        return "IN RW";
    }

    return "CAN NOT DETECT";
}

void pepa_set_fd_operations(pepa_core_t *core, const int fd, const int op)
{
    if (fd == core->sockets.shva_rw) {
        core->sockets.shva_rw_operation = op;
    }

    if (fd == core->sockets.out_write) {
        core->sockets.out_write_operation = op;
    }
}

int if_is_socket_valid(int sock)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        // The socket is not valid
        if (errno == EBADF) {
            // The file descriptor is bad, socket is closed or invalid
            slog_error_l("Socket is invalid or closed");
            return -1;
        } else {
            // Some other error occurred
            slog_error_l("Error checking socket: %s", strerror(errno));
            return -2;
        }
    }

    int error = 0;
    socklen_t len = sizeof(error);
    int ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);

    if (ret == 0 && error == 0) {
        // Socket is still valid and connected
        return 0;
    }
    // The socket might have been closed or is in an error state
    if (error != 0) {
        slog_error_l("Socket error: %s", strerror(error));
    } else {
        slog_error_l("Failed to get socket error: %s", strerror(errno));
    }

    return -3;
}

/*** TESTTING SOCKET SIZES ****/


int set_socket_blocking_mode(int sockfd)
{
    // Get the current file descriptor flags
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        slog_fatal_l("fcntl(F_GETFL) failed: %s", strerror(errno));
        abort();
    }

    // Clear the O_NONBLOCK flag to enable blocking mode
    // flags &= ~O_NONBLOCK;
    flags = flags & (~O_NONBLOCK);

    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        slog_fatal_l("fcntl(F_SETFL) failed: %s", strerror(errno));
        abort();
    }

#if 0 /* SEB */ /* 15/11/2024 */
    flags = fcntl(sockfd, F_GETFL, 0);

    if (flags == -1) {
        perror("fcntl(F_GETFL) failed");
    } else if (flags & O_NONBLOCK) {
        slog_fatal_l("Socket is still non-blocking\n");
    } else {
        slog_note_l("Socket is blocking\n");
    }
#endif /* SEB */ /* 15/11/2024 */

    return 0; // Successfully set the socket to blocking mode
}

const char *utils_socked_blocking_or_not(int socket_fd)
{
    int flags = fcntl(socket_fd, F_GETFL, 0);

    if (flags & O_NONBLOCK) {
        return "NON-BLOKING";
    } else {
        return "BLOKING";
    }
}

int utils_socked_blocking_or_not_int(int socket_fd)
{
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags & O_NONBLOCK) {
        return 0;
    }
    return 0;
}

int bytes_available_read(int sockfd)
{
    int bytes = 0;

    if (ioctl(sockfd, FIONREAD, &bytes) < 0) {
        slog_error_l("ioctl FIONREAD is failed: %s", strerror(errno));
        return -1;
    }

    return bytes;
}

int bytes_size_of_send_queue(int sockfd)
{
    int sndbuf_size;
    socklen_t optlen = sizeof(sndbuf_size);
    if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, &optlen) < 0) {
        slog_error_l("getsockopt SO_SNDBUF is failed: %s", strerror(errno));
        return -1;
    }

    return sndbuf_size;
}

int bytes_available_write(int sockfd)
{
    // Get the total size of the sending buffer
    int sndbuf_size;
    int bytes_in_queue;

    socklen_t optlen = sizeof(sndbuf_size);
    if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, &optlen) < 0) {
        slog_error_l("getsockopt SO_SNDBUF is failed: %s", strerror(errno));
        return -1;
    }

    // Get the current amount of data in the sending queue
    if (ioctl(sockfd, SIOCOUTQ, &bytes_in_queue) < 0) {
        slog_error_l("ioctl SIOCOUTQ is failed: %s", strerror(errno));
        return -1;
    }

    // Calculate available space in the sending buffer
    int available_space = sndbuf_size - bytes_in_queue;
    return available_space;
}

/*** SOCKET SEND / RECV WRAPPERS ****/

ssize_t send_exact_flags(const int sock_fd, const char *buf, const size_t num_bytes, int flags)
{
    int available;
    int sent_retry = 0;
    size_t total_sent = 0;

    size_t waiting_time = SEC_TO_USEC(SEND_WAIT_SEC);
    size_t retry_time = SLEEP_BETWEEN_TRY_USEC;

    /* First, try to wait until the sending queue has anough space to send */
    for (size_t idx = 0; idx < waiting_time; idx += retry_time) {
        available = bytes_available_write(sock_fd);
        if (available < 0) {
            slog_error_l("Can not get available size for socket (FD = %d)", sock_fd);
            return -1;
        }

        if (num_bytes > (size_t)available) {
            usleep(100000);
        }
    }

    available = bytes_available_write(sock_fd);
    if (available < 0) {
        slog_error_l("Can not get available size for socket (FD = %d)", sock_fd);
        return -1;
    }

    while (total_sent < num_bytes) {
        ssize_t bytes_sent = send(sock_fd, buf + total_sent, num_bytes - total_sent, flags);

        if (bytes_sent < 1) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, try again
            }

            slog_error_l("Error while sending: sent %lu bytes, expected to send %lu, error is: %s", total_sent, num_bytes, strerror(errno));
            return (-1); // Error occurred
        }
        total_sent += bytes_sent;
    }
    if (sent_retry > 0) {
        slog_debug_l("RET: %d, sent %lu bytes, asked to send: %lu", sent_retry, total_sent, num_bytes);
    }
    return (total_sent);
}

ssize_t send_exact(const int sock_fd, const char *buf, const size_t num_bytes)
{
    return send_exact_flags(sock_fd, buf, num_bytes, 0);
}

ssize_t send_exact_more(const int sock_fd, const char *buf, const size_t num_bytes)
{
    return send_exact_flags(sock_fd, buf, num_bytes, MSG_MORE);
}

ssize_t send_exact_to_file(const int sock_fd, const char *buf, const size_t num_bytes, const char *name, const int instance)
{
    int resent = 0;
    size_t total_sent = 0;
    while (total_sent < num_bytes) {
        // ssize_t bytes_sent = send(sock_fd, buf + total_sent, num_bytes - total_sent, 0);
        ssize_t bytes_sent = write(sock_fd, buf + total_sent, num_bytes - total_sent);

        if (bytes_sent < 1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, try again
            }

            if (errno == EAGAIN && resent < 10) {
                // if (errno == EAGAIN) {
                usleep(10000);
                resent++;
                continue;  // Interrupted by signal, try again
            }


            slog_error_l("[%s][%i] Error while sending: sent %lu bytes, expected to send %lu, error is: %s",
                         name, instance, total_sent, num_bytes, strerror(errno));
            return (-1);  // Error occurred

        }

        total_sent += bytes_sent;
    }
    return (total_sent);
}

int check_socket_status(int sockfd)
{
    int error = 0;
    socklen_t len = sizeof(error);

    // Use getsockopt to retrieve the SO_ERROR option from the socket
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        slog_note_l("getsockopt failed: %s", strerror(errno));
        slog_error_l("getsockopt failed: %s", strerror(errno));
        return (-1);
    }

    if (error == 0) {
        slog_note_l("Socket is operating normally, no errors\n");
        return (0);
    }
    // Print the specific error message
    fprintf(stderr, "Socket error: %s\n", strerror(error));
    slog_error_l("Socket has an error: %s\n", strerror(error));
    return (-1);
}



/**
 * @author Sebastian Mountaniol (29/10/2024)
 * @brief Receive asked number of bytes from the socket.
 * @param int sock_fd Sockert fd
 * @param char * buf Buffer to read to
 * @param size_t num_bytes Number of bytes to receive
 * @return ssize_t Number of bytes received on success, -1 on failure
 * @details
 */
ssize_t recv_exact(const int sock_fd, char *buf, const size_t buf_size, const size_t num_bytes, const char *caller, const int line)
{
    int available;
    int reread = 0;
    size_t total_received = 0;

    if (buf_size < num_bytes) {
        slog_fatal_l("Asked to receive a buffer (%zu) > available space (%zu) (FD = %d) from %s +%d", num_bytes, buf_size, sock_fd, caller, line);
        abort();
    }

    size_t waiting_time = SEC_TO_USEC(RECV_WAIT_SEC);
    size_t retry_time = SLEEP_BETWEEN_TRY_USEC;

    /* First, try to wait until the sending queue has anough space to send */
    for (size_t idx = 0; idx < waiting_time; idx += retry_time) {
        available = bytes_available_read(sock_fd);
        if (available < 0) {
            slog_error_l("Can not get available size for socket read (FD = %d)", sock_fd);
        }

        if (num_bytes > (size_t)available) {
            usleep(retry_time);
        }
    }

    available = bytes_available_read(sock_fd);
    if (available < 0) {
        slog_error_l("Not available size of bytes to read from the socket (FD = %d)", sock_fd);
    }

    // slog_note_l("Asked to receive a buffer (%zu), buf size (%zu), (FD = %d) from %s +%d", num_bytes, buf_size,
    /// sock_fd, caller, line);

    while (total_received < num_bytes) {
        // ssize_t bytes_received = recv(sock_fd, buf + total_received, num_bytes - total_received, 0);
        ssize_t bytes_received = read(sock_fd, buf + total_received, num_bytes - total_received);
        if (bytes_received < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, try again
            }

            if (EAGAIN == errno && reread < 10) {
                usleep(100000);
                reread++;
                continue;
            }
            {
                slog_error_l("(FD = %d) recv returned -1, total_received: %zu, asked to receive: %zu, errno = %d, %s", sock_fd, total_received, num_bytes, errno, strerror(errno));
                int rc = check_socket_status(sock_fd);
                if (0 == rc) {
                    slog_note_l("The socket looks normal");
                }
                return (-1);  // Error occurred
            }
        } else if (bytes_received == 0) {
            slog_error_l("recv returned 0: %s", strerror(errno));
            // check_socket_status(sock_fd);
            // Connection was closed by the peer
            break;
        }
        total_received += bytes_received;
    }
    return (total_received == num_bytes ? (ssize_t)total_received : -1); // Return -1 if we didn't get the exact bytes
}

