#define _GNU_SOURCE
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <ctype.h>

#include "logger.h"
#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"

static unsigned int pepa_gen_ticket(unsigned int seed);
static size_t pepa_add_id_and_ticket(pepa_core_t *core, char *buf, const size_t buf_size);

#define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

void set_sig_handler(void)
{
    sigset_t set;
    sigfillset(&set);

    int sets = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (sets != 0) {
        handle_error_en(sets, "pthread_sigmask");
        exit(4);
    }
}

int32_t pepa_pthread_init_phase(const char *name)
{
    llog_n("####################################################");
    llog_n("Thread %s: Detaching", name);
    if (0 != pthread_detach(pthread_self())) {
        llog_f("Thread %s: can't detach myself", name);
        perror("Thread : can't detach myself");
        return -PEPA_ERR_THREAD_DETOUCH;
    }

    llog_n("Thread %s: Detached", name);
    llog_n("####################################################");

    int32_t rc = pthread_setname_np(pthread_self(), name);
    if (0 != rc) {
        llog_f("Thread %s: can't set name", name);
    }

    return PEPA_ERR_OK;
}

void pepa_parse_pthread_create_error(const int32_t rc)
{
    switch (rc) {
    case EAGAIN:
        llog_f("Insufficient resources to create another thread");
        break;
    case EINVAL:
        llog_f("Invalid settings in attr");
        break;
    case EPERM:
        llog_f("No permission to set the scheduling policy and parameters specified in attr");
        break;
    default:
        llog_f("You should never see this message: error code %d", rc);
    }
}

void pepa_set_tcp_timeout(const int sock)
{
    struct timeval time_out;
    time_out.tv_sec = 1;
    time_out.tv_usec = 0;

    if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out))) {
        llog_d("[from %s] SO_RCVTIMEO has a problem: %s", "EMU SHVA", strerror(errno));
    }
    if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&time_out, sizeof(time_out))) {
        llog_d("[from %s] SO_SNDTIMEO has a problem: %s", "EMU SHVA", strerror(errno));
    }
}

void pepa_set_tcp_recv_size(const pepa_core_t *core, const int sock)
{
    uint32_t buf_size = core->internal_buf_size;

    /* Set TCP receive window size */
    if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&buf_size, sizeof(buf_size))) {
        llog_d("[from %s] SO_RCVBUF has a problem: %s", "EMU SHVA", strerror(errno));
    }
}

void pepa_set_tcp_send_size(const pepa_core_t *core, const int sock)
{
    uint32_t buf_size = core->internal_buf_size;
    /* Set TCP sent window size */
    if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&buf_size, sizeof(buf_size))) {
        llog_d("[from %s] SO_SNDBUF has a problem: %s", "EMU SHVA", strerror(errno));
    }
}

/* Find the next occurence of the character 'c' in the buffer 'buf' which len is 'len'; start search from the offset 'offset' */
/**
 * @author Sebastian Mountaniol (1/28/24)
 * @brief Find the next occurence of the character 'c' in the buffer 'buf' which len is 'len'; start search from the offset 'offset'
 * @param char* buf Buffer to search in  
 * @param int len   Len of the buffer
 * @param int offset Start search from this offset
 * @param char c     Search for this character
 * @return int Return the offset of found character; if not found, the size of the buffer returned
 * @details 
 */
#if 0 /* SEB */
static int find_next(char *buf, int len, int offset, char c){
    int now = offset;
    do {
        if (buf[now] == c) {
            break;
        }
        now++;
    } while (now < len);

    return now;
}
#endif

#if 0 /* SEB */
static int find_unprintable_next(char *buf, int len, int offset){
    int now = offset;
    do {
        if (buf[now] == c) {
            break;
        }
        now++;
    } while (now < len);

    return now;
}
#endif

/* Print message */
#if 0 /* SEB */
static void pepa_print_buffer(pepa_core_t *core, char *buf, const ssize_t rx,
                              const int fd_out, const char *name_out,
                              const int fd_in, const char *name_in){
    // static char *print_buf = NULL;
    //char   print_buf[1024];
    ssize_t offset  = 0;
    int     prc     = 0; /** < Status from snprintf */
    int     mes_num = 1; /**< Offset inside of the received buffer */

    if (NULL == core->print_buf) {
        /* We need a bigger buffer since we can add more information into debug print */
        core->print_buf = calloc(core->print_buf_len, 1);
        llog_w("Allocated printing buffer");
    }

    do {
        //memset(core->print_buf, 0, core->internal_buf_size * 1024);
        size_t current_len = strlen(buf + offset);

        prc = snprintf(core->print_buf, 1024, "+++ MES[%d]: %s [fd:%.2d] -> %s [fd:%.2d], LEN:%zu, OFFSET:%zd, TOTAL:%zd, |%s|",
                       /* mes num */ mes_num,
                       name_in, fd_in,
                       name_out, fd_out,
                       current_len, offset, rx,
                       buf + offset);
        if (prc < 1) {
            break;
        }

        core->print_buf[prc] = '\0';

        llog_n("%s", core->print_buf);

        if (offset + 1 < rx) {
            offset = find_next(buf, rx, offset + 1, 0);
        }
        offset++;
        mes_num++;
    } while (offset < rx);
}
#endif

/* Print message */
#if 0 /* SEB */
static void pepa_print_buffer_old(pepa_core_t *core, char *buf, const ssize_t rx,
                                  const int fd_out, const char *name_out,
                                  const int fd_in, const char *name_in){
    ssize_t offset  = 0;
    int     prc     = 0; /** < return value of snprintf */
    int     mes_num = 1; /**< Message number inside the buffer */

    /* On the first message we allocate a buffer for printing */
    if (NULL == core->print_buf) {
        core->print_buf = calloc(core->print_buf_len, 1);
    }

    if (NULL == core->print_buf) {
        llog_e("Can not allocate printing buffer, asked size is: %u; message was not printed", core->print_buf_len);
        return;
    } else {
        llog_w("Allocated printing buffer of size %u", core->print_buf_len);
    }

    do {
        size_t current_len = strlen(buf + offset);
        prc = snprintf(core->print_buf, core->print_buf_len, "+++ MES[%d]: %s [fd:%.2d] -> %s [fd:%.2d], LEN:%zu, OFFSET:%zd, TOTAL:%zd, |%s|",
                       /* mes num */ mes_num,
                       name_in, fd_in,
                       name_out, fd_out,
                       current_len, offset, rx,
                       buf + offset);

        if (prc < 1) {
            break;
        }

        /* Add the 0 terminator at the end */
        core->print_buf[prc] = '\0';

        /* Print */
        llog_d("%s", core->print_buf);

        if (offset + 1 < rx) {
            offset = find_next(buf, rx, offset + 1, 0);
        }
        offset++;
        mes_num++;
    } while (offset < rx);
}
#endif

/* Print message */
static void pepa_print_buffer(pepa_core_t *core, char *buf, const ssize_t rx,
                              const int fd_out, const char *name_out,
                              const int fd_in, const char *name_in)
{
    char pre_buffer[64];
    ssize_t offset = 0;
    ssize_t header_offset = 0;

    int     prc    = 0; /** < return value of snprintf */

    /* If there is PEPA ID and Ticket, then print them first to the dedicated buffer */

    /* On the first message we allocate a buffer for printing */
    if (NULL == core->print_buf) {
        core->print_buf = calloc(core->print_buf_len, 1);
    }

    if (NULL == core->print_buf) {
        llog_e("Can not allocate printing buffer, asked size is: %u; message was not printed", core->print_buf_len);
        return;
    } 
#if 0 /* SEB */ /* 30.09.2024 */
else {
        llog_w("Allocated printing buffer of size %u", core->print_buf_len);
    }
#endif /* SEB */ /* 30.09.2024 */ 

    /* Print header */
    offset = snprintf(core->print_buf, core->print_buf_len, "+++ MES: %s [fd:%.2d] -> %s [fd:%.2d], LEN:%zd |",
                      /* mes num */ name_in, fd_in, name_out, fd_out, rx);

    for (int index = 0; index < rx; index++) {
        if (isprint(buf[index])) {
            core->print_buf[offset] = buf[index];
            offset++;

        } else {
            prc = snprintf(core->print_buf + offset, core->print_buf_len - offset, "<0X%X>", (unsigned int)buf[index]);
            if (prc < 1) {
                llog_e("Can not print non-printable character from, offset of the character: %d; stopped printing", index);
                return;
            }

            offset += prc;
        }
    }

    core->print_buf[offset] = '|';
    offset++;
    core->print_buf[offset] = '\0';

    /* Print */
    // llog_d("%s", core->print_buf);
    // slogn("%s", core->print_buf);
    char *copy_buf = strndup(core->print_buf, offset + 1);
    if (copy_buf) {
        logger_push(LOGGER_DEBUG, copy_buf, offset, __FILE__, __LINE__);
    } else {
        llog_e("Could not copy printing buffer!");
    }
}

/**
 * @author se (9/24/24)
 * @brief This function generate a pseudorandom number.
 *        This number is not really random but we do not need
 *        real randomness. We need to generate a unique number
 *        for every next buffer. This function is a very fast
 *        and efficient one instead.
 * @param seed   A previous value of the pseudorandom number
 * @return unsigned int The next pseudorandom number
 */
static unsigned int pepa_gen_ticket(unsigned int seed)
{
    return (214013 * seed + 2531011);
}

/**
 * 
 * @author se (9/24/24)
 * @brief This function adds a ticket and PEPA ID to the buffer
 *        beginning, if they are enabled
 * @param core     Core structure
 * @param buf      Buffer allocated for sending
 * @param buf_size Size of the buffer
 * @return size_t If a ticket and/or PEPA ID were added,
 *         the offset returned is the length of written ticket
 *         and/or PEPA ID. If they both are disabled, 0 will be
 *         returned.
 */
__attribute__((hot))
static size_t pepa_add_id_and_ticket(pepa_core_t *core, char *buf, const size_t buf_size)
{
    size_t              offset = 0;
    static unsigned int ticket;

    if (core->use_ticket) {
        /* If the buffer is too short, we return 0 and do nothing */
        if (buf_size < offset + sizeof(unsigned int)) {
            return offset;
        }

        ticket = pepa_gen_ticket(ticket);
        memcpy(buf + offset, &ticket, sizeof(unsigned int));
        offset += sizeof(unsigned int);
    }

    if (core->use_id) {
        /* If the buffer is too short, we return the offset and do nothing */
        if (buf_size < offset + sizeof(unsigned int)) {
            return offset;
        }

        memcpy(buf + offset, &core->id_val, sizeof(int));
        offset += sizeof(int);
    }

    /* Return the offset, means, how many bytes we used in the buffer beginning */
    return offset;
}

__attribute__((hot))
int pepa_one_direction_copy3(pepa_core_t *core,
                             const int fd_out, const char *name_out,
                             const int fd_in, const char *name_in,
                             char *buf, const size_t buf_size,
                             const int do_debug,
                             uint64_t *ext_rx, uint64_t *ext_tx,
                             const int max_iterations)
{
    //static char *print_buf   = calloc(core->internal_buf_size + 1, 1);

    int     ret          = PEPA_ERR_OK;
    ssize_t rx_bytes     = 0;
    ssize_t tx_total     = 0;
    ssize_t rx_total     = 0;
    int     iteration    = 0;

    /* buf_size_use - how many bytes available in the buffer after we reserved and added service information */
    size_t  buf_size_use = buf_size;
    ssize_t offset       = 0;

    /* If message dump is enabled, keep 1 character for \0 */
    if (core->dump_messages) {
        buf_size_use--;
    }

    if (do_debug) {
        // llog_n("Starrting transfering from %s to %s", name_in, name_out);
    }

    do {
        ssize_t tx_bytes   = 0;
        ssize_t tx_current = 0;

        iteration++;
        if (do_debug) {
            // llog_n("Iteration: %d", iteration);
        }

        offset = pepa_add_id_and_ticket(core, buf, buf_size);

        /* Read one time, then we will transfer it possibly in pieces */
        rx_bytes = read(fd_in, buf + offset, buf_size_use - offset);

        if (do_debug) {
            // llog_n("Iteration: %d, finised read(), there is %d bytes", iteration, rx);
        }


        if (rx_bytes < 0) {
            if (do_debug) {
                llog_w("Could not read: from read sock %s [%d]: %s", name_in, fd_in, strerror(errno));
            }
            ret = -PEPA_ERR_BAD_SOCKET_READ;
            goto endit;
        }

        /* nothing to read on the furst iteraion; it means, this socket is invalid */
        if ((0 == rx_bytes) && (1 == iteration)) {
            /* If we can not read on the first iteration, it probably means the fd was closed */
            ret = -PEPA_ERR_BAD_SOCKET_READ;

            if (do_debug) {
                llog_e("Could not read on the first iteration: from read sock %s [%d] out socket %s [%d}: %s",
                             name_in, fd_in, name_out, fd_out, strerror(errno));
            }
        }

        if (PEPA_ERR_OK != ret) {
            goto endit;
        }

        /* It is possible we recevived several messages;
         * the messages are divided by null terminator.
           We must print until the whole buffer is printed */

        if (core->dump_messages) {
            pepa_print_buffer(core, buf, rx_bytes + offset, fd_out, name_out, fd_in, name_in);
        }

        /* Write until the whole received buffer was transferred */
        do {
            tx_current  = write(fd_out, buf, (size_t)(rx_bytes + offset - tx_bytes));

            if (tx_current <= 0) {
                ret = -PEPA_ERR_BAD_SOCKET_WRITE;
                if (do_debug) {
                    llog_w("Could not write to write to sock %s [%d]: returned < 0: %s", name_out, fd_out, strerror(errno));
                }
                goto endit;
            }

            tx_bytes += tx_current;

            /* If we still not transfered everything, give the TX socket some time to rest and finish the transfer */
            if (tx_bytes < (rx_bytes + offset)) {
                usleep(10);
            }

        } while (tx_bytes < rx_bytes + offset);

        rx_total += rx_bytes;
        tx_total += tx_bytes;
        if (do_debug) {
            // llog_n("Iteration %d done: rx = %d, tx = %d", iteration, rx, tx);
        }

        /* Run this loop as long as we have data on read socket, but no more that max_iterations */
    } while (((int32_t)buf_size_use == (rx_bytes + offset)) && (iteration <= max_iterations));

    ret = PEPA_ERR_OK;
endit:
    if (do_debug) {
        // llog_n("Finished transfering from %s to %s, returning %d, rx = %d, tx = %d, ", name_in, name_out, ret, rx_total, tx_total);
    }

    if (rx_total > 0) {
        *ext_rx += (uint64_t)rx_total;

    }

    if (tx_total > 0) {
        *ext_tx += (uint64_t)tx_total;

    }

    return ret;
}

int32_t pepa_test_fd(const int fd)
{
    if ((fcntl(fd, F_GETFL) < 0) && (EBADF == errno)) {
        return -PEPA_ERR_FILE_DESCRIPTOR;
    }
    return PEPA_ERR_OK;
}

int32_t epoll_ctl_add(const int epfd, const int sock, const uint32_t events)
{
    struct epoll_event ev;

    if (sock < 0) {
        llog_f("Tryed to add fd < 0: %d", sock);
        return -PEPA_ERR_FILE_DESCRIPTOR;
    }

    ev.events = events;
    ev.data.fd = sock;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        llog_f("Can not add fd %d to epoll: %s", sock, strerror(errno));
        return -PEPA_ERR_EPOLL_CANNOT_ADD;
    }

    return PEPA_ERR_OK;
}

int32_t pepa_socket_shutdown_and_close(const int sock, const char *my_name)
{
    if (sock < 0) {
        llog_w("%s: Looks like the socket is closed: == %d", my_name, sock);
        return -PEPA_ERR_FILE_DESCRIPTOR;
    }

    int rc = shutdown(sock, SHUT_RDWR);
    if (rc < 0) {
        llog_w("%s: Could not shutdown the socket: fd: %d, %s", my_name, sock, strerror(errno));
        return -PEPA_ERR_FILE_DESCRIPTOR;
    }

    rc = close(sock);
    if (rc < 0) {
        llog_w("%s: Could not close the socket: fd: %d, %s", my_name, sock, strerror(errno));
        return -PEPA_ERR_CANNOT_CLOSE;
    }

    llog_n("%s: Closed socket successfully %d", my_name, sock);
    return PEPA_ERR_OK;
}

void pepa_socket_close(const int socket, const char *socket_name)
{
    int rc;
    int i;
    if (socket < 0) {
        llog_n("Can not close socket %s, its value is %d: probaly it is closed", socket_name, socket);
        return;
    }

    /* Try to shutdown the socket before it closed */
    rc = shutdown(socket, SHUT_RDWR);
    if (rc < 0) {
        llog_n("%s: Could not shutdown the socket: fd: %d, %s", socket_name, socket, strerror(errno));
    }

    for (i = 0; i < 256; i++) {
        rc = close(socket);
        if (0 == rc) {
            llog_n("## Closed socket socket %s : %d, iterations: %d", socket_name, socket, i);
            return;
        }
        usleep(100);
        llog_e("Can not close socket %s, error %d: %s, iteration: %d", socket_name, rc, strerror(errno), i);
    }
}

void pepa_reading_socket_close(const int socket, const char *socket_name)
{
    int     i;
    char    buf[16];
    int     iterations = 0;
    ssize_t read_from  = 0;

    if (socket < 0) {
        llog_n("Tried to close socket %s, its value is %d: probably is closed", socket_name, socket);
        return;
    }

    ssize_t rc = close(socket);
    if (0 == rc) {
        llog_n("## Closed from the first try socket socket %s, iterations: %d ", socket_name, iterations);
        return;
    }

    /* Try to read from socket everything before it closed */
    for (i = 0; i < 2048; i++) {
        rc = read(socket, buf, 16);
        if (read(socket, buf, 16) < 0) {
            i = 2048;
            continue;
        }
        read_from += rc;
        iterations++;
    }

    pepa_socket_close(socket, socket_name);
    llog_n("## Closed socket socket %s, iterations: %d ", socket_name, iterations);
}

void pepa_socket_close_in_listen(pepa_core_t *core)
{
    if (FD_CLOSED == core->sockets.in_listen) {
        return;
    }

    if (PEPA_ERR_OK != pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN")) {
        llog_w("Close and shutdown of IN LISTEN is failed");
    }
    core->sockets.in_listen = FD_CLOSED;
    llog_n("Closed core->sockets.in_listen");
}

__attribute__((nonnull(1, 2)))
int pepa_open_listening_socket(struct sockaddr_in *s_addr,
                               const buf_t *ip_address,
                               const uint16_t port,
                               const int num_of_clients,
                               const char *name)
{
    int rc   = PEPA_ERR_OK;
    int sock;

    //llog_d("Open Socket [from %s]: starting for %s:%d, num of clients: %d", name, ip_address->data, port, num_of_clients);

    memset(s_addr, 0, sizeof(struct sockaddr_in));
    s_addr->sin_family = (sa_family_t)AF_INET;
    s_addr->sin_port = htons(port);

    //llog_d("Open Socket [from %s]: Port is %d", name, port);

    const int inet_aton_rc = inet_aton(ip_address->data, &s_addr->sin_addr);

    //llog_n("Open Socket [from %s]: specified address, use %s", name, ip_address->data);
    /* Warning: inet_aton() returns 1 on success */
    if (1 != inet_aton_rc) {
        llog_f("Open Socket [from %s]: Could not convert string address to in_addr_t: %s", name, strerror(errno));
        return (-PEPA_ERR_CONVERT_ADDR);
    }

    // llog_n("Open Socket [from %s]: Going to create socket for %s:%d", name, ip_address->data, port);
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock  < 0) {
        llog_e("Open Socket [from %s]: Could not create listen socket: %s", name, strerror(errno));
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    const int enable = 1;
    rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (rc < 0) {
        llog_e("Open Socket [from %s]: Could not set SO_REUSEADDR on socket, error: %s", name, strerror(errno));
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    // pepa_set_tcp_connection_props(core, sock);

    rc = bind(sock, (struct sockaddr *)s_addr, (socklen_t)sizeof(struct sockaddr_in));
    if (rc < 0 && EADDRINUSE == errno) {
        llog_w("Address in use, can't bind, [from %s], waiting...", name);
        // sleep(10);
        return -PEPA_ERR_SOCKET_BIND;
    }

    if (rc < 0) {
        llog_e("Open Socket [from %s]: Can't bind: %s", name, strerror(errno));
        close(sock);
        return (-PEPA_ERR_SOCKET_IN_USE);
    }

    rc = listen(sock, num_of_clients);
    if (rc < 0) {
        llog_f("Open Socket [from %s]: Could not set SERVER_CLIENTS: %s", name, strerror(errno));
        close(sock);
        return (-PEPA_ERR_SOCKET_LISTEN);
    }

    //llog_n("Open Socket [from %s]: Opened listening socket: %d", name, sock);
    return (sock);
}

int pepa_open_connection_to_server(const char *address, const uint16_t port, const char *name)
{
    struct sockaddr_in s_addr;
    int                sock;

    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family = (sa_family_t)AF_INET;

    llog_n("[from %s]: Converting addr string to binary: |%s|", name, address);
    const int convert_rc = inet_pton(AF_INET, address, &s_addr.sin_addr);
    if (0 == convert_rc) {
        llog_f("[from %s]: The string is not a valid IP address: |%s|", name, address);
        return (-PEPA_ERR_CONVERT_ADDR);
    }

    if (convert_rc < 0) {
        llog_f("[from %s]: Could not convert string addredd |%s| to binary", name, address);
        return (-PEPA_ERR_CONVERT_ADDR);
    }

    s_addr.sin_port = htons(port);

    llog_n("[from %s]: Creating socket: |%s|", name, address);
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        llog_f("[from %s]: Could not create socket", name);
        //PEPA_TRY_ABORT();
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    llog_n("[from %s]: Starting connect(): |%s|", name, address);
    if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
        llog_n("[from %s]: Could not connect to server: %s", name, strerror(errno));
        close(sock);
        //PEPA_TRY_ABORT();
        return (-PEPA_ERR_SOCK_CONNECT);
    }

    llog_n("[from %s]: Opened connection to server: %d", name, sock);
    return (sock);
}

int pepa_find_socket_port(const int sock)
{
    struct sockaddr_in sin;
    socklen_t          len = sizeof(sin);
    if (sock < 0) {
        return -1;
    }

    if (getsockname(sock, (struct sockaddr *)&sin, &len) == -1) {
        perror("getsockname");
        return -1;
    } else {
        return ntohs(sin.sin_port);
    }
}

