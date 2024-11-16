#define _GNU_SOURCE
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <ctype.h>

#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_ticket_id.h"
#include "pepa_utils.h"

#define handle_error_en(en, msg) \
    do                           \
    {                            \
        errno = en;              \
        perror(msg);             \
        exit(EXIT_FAILURE);      \
    } while (0)

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
    slog_note("####################################################");
    slog_note_l("Thread %s: Detaching", name);
    if (0 != pthread_detach(pthread_self())) {
        slog_fatal_l("Thread %s: can't detach myself", name);
        perror("Thread : can't detach myself");
        return (-PEPA_ERR_THREAD_DETOUCH);
    }

    slog_note_l("Thread %s: Detached", name);
    slog_note("####################################################");

    int32_t rc = pthread_setname_np(pthread_self(), name);
    if (0 != rc) {
        slog_fatal_l("Thread %s: can't set name", name);
    }

    return (PEPA_ERR_OK);
}

void pepa_parse_pthread_create_error(const int32_t rc)
{
    switch (rc) {
        case EAGAIN:
            slog_fatal_l("Insufficient resources to create another thread");
            break;
        case EINVAL:
            slog_fatal_l("Invalid settings in attr");
            break;
        case EPERM:
            slog_fatal_l("No permission to set the scheduling policy and parameters specified in attr");
            break;
        default:
            slog_fatal_l("You should never see this message: error code %d", rc);
    }
}

void pepa_set_tcp_timeout(const int sock)
{
    struct timeval time_out;
    time_out.tv_sec = 0;
    time_out.tv_usec = 0;

    if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out))) {
        slog_debug_l("[from %s] SO_RCVTIMEO has a problem: %s", "EMU SHVA", strerror(errno));
    }

    if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&time_out, sizeof(time_out))) {
        slog_debug_l("[from %s] SO_SNDTIMEO has a problem: %s", "EMU SHVA", strerror(errno));
    }

    int rc = set_socket_blocking_mode(sock);
    if (rc) {
        slog_fatal_l("Can not set socket into blocking mode");
        abort();
    }
}

void pepa_set_tcp_recv_size(const pepa_core_t *core, const int sock, const char *name)
{
    uint32_t buf_size = core->internal_buf_size;

    /* Set TCP receive window size */
    if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&buf_size, sizeof(buf_size))) {
        slog_debug_l("[from %s] SO_RCVBUF has a problem: %s", name, strerror(errno));
    } else {
        slog_note_l("[from %s] SO_RCVBUF is set succsesful", name);
    }

    int rc = set_socket_blocking_mode(sock);
    if (rc) {
        slog_fatal_l("Can not set socket into blocking mode");
        abort();
    }
}

void pepa_set_tcp_send_size(const pepa_core_t *core, const int sock, const char *name)
{
    uint32_t buf_size = core->internal_buf_size;
    /* Set TCP sent window size */
    if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&buf_size, sizeof(buf_size))) {
        slog_debug_l("[from %s] SO_SNDBUF has a problem: %s", "EMU SHVA", strerror(errno));
    } else {
        slog_note_l("[from %s] SO_SNDBUF is set succsesful", name);
    }

    int rc = set_socket_blocking_mode(sock);
    if (rc) {
        slog_fatal_l("Can not set socket into blocking mode");
        abort();
    }
}

/**
 * @author Sebastian Mountaniol (20/10/2024)
 * @brief Test whether a Ticket should be used to buffer
 * @param pepa_core_t * core PEPA Core structure
 * @param int fd_out TX file descriptor
 * @return int 1 if a Ticket should be added, 0 otherwise
 * @details A Ticket should be added, if tickets eabled in the core structure (on the start it is parsed) and
 *    if the fd_ou is OUT Write socket
 */
static int pepa_use_ticket(const pepa_core_t *core, const int fd_out)
{
    if (core->sockets.out_write == fd_out && core->use_ticket) {
        return (1);
    }
    return (0);
}

/**
 * @author Sebastian Mountaniol (20/10/2024)
 * @brief Test whether the PEPA ID should be used to buffer
 * @param pepa_core_t * core PEPA Core structure
 * @param int fd_out TX file descriptor
 * @return int 1 if the PEPA ID should be added, 0 otherwise
 * @details A PEPA ID should be added, if PEPA ID is eabled in the core structure (on the start it is parsed)
 *    and if the fd_ou is OUT Write socket
 */
static int pepa_use_pepa_id(const pepa_core_t *core, const int fd_out)
{
    if (core->sockets.out_write == fd_out && core->use_id) {
        return (1);
    }
    return (0);
}

// #define DEB_BUF (128)
/* Print message */
static void pepa_print_buffer2(pepa_core_t *core,
                               const buf_and_header_t *bufh,
                               const int fd_out,
                               const char *name_out,
                               const int fd_in,
                               const char *name_in)
{
    // char    deb_buffer[DEB_BUF];
    /* offset: offset inside core->print_buffer*/
    size_t offset = 0;
    /* in_buf_offset: offset inside the binary buffer we need to print out */
    size_t in_buf_offset = 0;

    int prc = 0; /** < return value of snprintf */

    /* If there is PEPA ID and Ticket, then print them first to the dedicated buffer */

    /* On the first message we allocate a buffer for printing */
    if (NULL == core->print_buf) {
        core->print_buf = calloc(core->print_buf_len, 1);
    }

    if (NULL == core->print_buf) {
        // llog_e("Can not allocate printing buffer, asked size is: %u; message was not printed", core->print_buf_len);
        slog_error_l("Can not allocate printing buffer, asked size is: %u; message was not printed", core->print_buf_len);
        return;
    }

    /* If there is PEPA ID and Ticket, then print them first to the dedicated buffer */
    /* Print header */
    offset += snprintf(core->print_buf + offset, core->print_buf_len, "+++ MES: %s [fd:%.2d] -> %s [fd:%.2d], LEN:%zu ",
                       /* mes num */ name_in, fd_in, name_out, fd_out, bufh->buf_used);

    /* If tickets are used, print it */
    if (bufh->prebuf) {
        int rc = snprintf(core->print_buf + offset, 32, "[TICKET: 0X%X] [ID: 0X%X] ", bufh->prebuf->ticket, bufh->prebuf->pepa_id);
        offset += rc;
        in_buf_offset += rc;
    }

    // offset += snprintf(core->print_buf + offset, 1, "%s", 1);
    core->print_buf[offset] = '|';
    offset++;

    /* Now we transform the binary buffer into a string form byte by byte */
    for (ssize_t index = 0; index < ((ssize_t)bufh->buf_used - ((ssize_t)in_buf_offset)); index++) {
        if (isprint(bufh->buf[in_buf_offset + index])) {
            core->print_buf[offset] = bufh->buf[in_buf_offset + index];
            offset++;
        } else {
            unsigned int current_char = (unsigned int)bufh->buf[index];
            prc = snprintf(core->print_buf + offset, core->print_buf_len - offset, "<0X%X>", current_char);
            if (prc < 1) {
                // llog_e("Can not print non-printable character from, offset of the character: %lu; stopped printing", index);
                slog_error_l("Can not print non-printable character from, offset of the character: %ld; stopped printing", index);
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
        // logger_push(LOGGER_DEBUG, copy_buf, offset, __FILE__, __LINE__);
        slog_note_l("%s", copy_buf);
    } else {
        // llog_e("Could not copy printing buffer!");
        slog_error_l("Could not copy printing buffer!");
    }

    free(copy_buf);
}

static void pepa_print_buffer(pepa_core_t *core, char *buf, const ssize_t rx,
                              const int fd_out, const char *name_out,
                              const int fd_in, const char *name_in)
{
    // char    deb_buffer[DEB_BUF];
    /* offset: offset inside core->print_buffer*/
    size_t offset = 0;
    /* in_buf_offset: offset inside the binary buffer we need to print out */
    size_t in_buf_offset = 0;

    int prc = 0; /** < return value of snprintf */

    /* If there is PEPA ID and Ticket, then print them first to the dedicated buffer */

    /* On the first message we allocate a buffer for printing */
    if (NULL == core->print_buf) {
        core->print_buf = calloc(core->print_buf_len, 1);
    }

    if (NULL == core->print_buf) {
        // llog_e("Can not allocate printing buffer, asked size is: %u; message was not printed", core->print_buf_len);
        slog_error_l("Can not allocate printing buffer, asked size is: %u; message was not printed", core->print_buf_len);
        return;
    }
    /* If there is PEPA ID and Ticket, then print them first to the dedicated buffer */
    /* Print header */
    offset += snprintf(core->print_buf + offset, core->print_buf_len, "+++ MES: %s [fd:%.2d] -> %s [fd:%.2d], LEN:%zd ",
                       /* mes num */ name_in, fd_in, name_out, fd_out, rx);

    /* If tickets are used, print it */
    if (pepa_use_ticket(core, fd_out)) {
        unsigned int ticket;
        memcpy(&ticket, buf + in_buf_offset, sizeof(unsigned int));
        offset += snprintf(core->print_buf + offset, 32, "[TICKET: 0X%X] ", ticket);
        // offset += snprintf(core->print_buf + offset, 32, "[TICKET: %X]", ticket);
        in_buf_offset += sizeof(unsigned int);
    }

    /* If Pepa ID is used, print it */
    if (pepa_use_pepa_id(core, fd_out)) {
        unsigned int pepa_id;
        memcpy(&pepa_id, buf + in_buf_offset, sizeof(unsigned int));
        offset += snprintf(core->print_buf + offset, 32, "[ID: 0X%X] ", pepa_id);
        in_buf_offset += sizeof(unsigned int);
    }

    // offset += snprintf(core->print_buf + offset, 1, "%s", 1);
    core->print_buf[offset] = '|';
    offset++;

    /* Now we transform the binary buffer into a string form byte by byte */
    for (ssize_t index = 0; index < (rx - ((ssize_t)in_buf_offset)); index++) {
        if (isprint(buf[in_buf_offset + index])) {
            core->print_buf[offset] = buf[in_buf_offset + index];
            offset++;
        } else {
            unsigned int current_char = (unsigned int)buf[index];
            prc = snprintf(core->print_buf + offset, core->print_buf_len - offset, "<0X%X>", current_char);
            if (prc < 1) {
                // llog_e("Can not print non-printable character from, offset of the character: %lu; stopped printing", index);
                slog_error_l("Can not print non-printable character from, offset of the character: %ld; stopped printing", index);
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
        // logger_push(LOGGER_DEBUG, copy_buf, offset, __FILE__, __LINE__);
        slog_note_l("%s", copy_buf);
    } else {
        // llog_e("Could not copy printing buffer!");
        slog_error_l("Could not copy printing buffer!");
    }

    free(copy_buf);
}

static void pepa_fill_prebuf(const pepa_core_t *core, pepa_prebuf_t *pre)
{
    static unsigned int ticket = 17;

    pre->ticket = pepa_gen_ticket(ticket);
    pre->pepa_id = core->id_val;
}

static buf_and_header_t *buf_and_header_t_alloc(char *buf, size_t buf_size)
{
    buf_and_header_t *bufh = malloc(sizeof(buf_and_header_t));
    if (NULL == bufh) {
        DE("Can't allocate\n");
        return (NULL);
    }

    memset(bufh, 0, sizeof(buf_and_header_t));
    bufh->buf = buf;
    bufh->buf_room = buf_size;

    return (bufh);
}

static void buf_and_header_set_prebuf(pepa_core_t *core, buf_and_header_t *bufh)
{
    TESTP_ASSERT(bufh, "bufh is NULL");
    bufh->prebuf_size = sizeof(pepa_prebuf_t);
    bufh->prebuf = calloc(bufh->prebuf_size, 1);
    TESTP_ASSERT(bufh->prebuf, "bufh->prebufis NULL");
    pepa_fill_prebuf(core, bufh->prebuf);
}

static void buf_and_header_t_release(buf_and_header_t *bufh)
{
    if (NULL == bufh) {
        DE("Arg is NULL\n");
        return;
    }

    /* Secure way: clear memory before release it */
#if 0 /* SEB */ /* 13/11/2024 */
    if (bufh->buf) {
        memset(bufh->buf, 0, bufh->buf_size);
        free(bufh->buf);
        bufh->buf = NULL;
    }
#endif /* SEB */ /* 13/11/2024 */

    if (bufh->prebuf) {
        memset(bufh->prebuf, 0, bufh->prebuf_size);
        free(bufh->prebuf);
        bufh->prebuf = NULL;
    }

    memset(bufh, 0, sizeof(buf_and_header_t));
    free(bufh);
}

static buf_and_header_t *pepa_socket_read2(pepa_core_t *core,
                                           const int fd_in,
                                           char *buf,
                                           const size_t buf_size,
                                           const char *name_in)
{
    int rc = 0;

    /* Test the socket for validity */
    rc = if_is_socket_valid(fd_in);
    if (rc) {
        slog_error_l("Socket %s (fd = %d) is invalid: %d", name_in, fd_in, rc);
        return NULL;
    }

    /* If the socket in non-blocking mode, reset it to blocking mode */
    if (utils_socked_blocking_or_not_int(fd_in)) {
        set_socket_blocking_mode(fd_in);
    }

    buf_and_header_t *bufh = buf_and_header_t_alloc(buf, buf_size);
    TESTP_ASSERT(bufh, "Can not alloc bufh");

    if (fd_in == core->sockets.shva_rw) {
        //slog_note_l("ADDING PEPA IS: src = %s, fd = %d", name_in, fd_in);
        buf_and_header_set_prebuf(core, bufh);
    }

    do {
        /* Read one time, then we will transfer it possibly in pieces */
        pepa_set_fd_operations(core, fd_in, 1);
        rc = read(fd_in, bufh->buf + bufh->buf_used, bufh->buf_room - bufh->buf_used);
        pepa_set_fd_operations(core, fd_in, 0);
        if (rc > 0) {
            bufh->buf_used += rc;
        }
    } while (rc < 1 && EINTR == errno);

    if (0 == bufh->buf_used) {
        slog_error_l("Could not read: from read sock %s (fd = %d) (Blocking? %s : %d) : %s",
                     name_in,
                     fd_in,
                     utils_socked_blocking_or_not(fd_in),
                     utils_socked_blocking_or_not_int(fd_in),
                     strerror(errno));

        buf_and_header_t_release(bufh);
        return NULL;
    }

    if (bufh->prebuf) {
        bufh->prebuf->pepa_len = bufh->buf_used;
    }
    return bufh;
}

#if 0 /* SEB */ /* 14/11/2024 */
static ssize_t pepa_socket_read(pepa_core_t *core,
                                __attribute__((unused)) size_t iteration,
                                const int fd_in,
                                __attribute__((unused)) const int fd_out,
                                char *buf,
                                const size_t buf_size,
                                __attribute__((unused)) const int do_debug,
                                const char *name_in,
                                __attribute__((unused)) const char *name_out){
    char *buf_ptr = buf;
    ssize_t rx_bytes = 0;
    // size_t  offset        = 0;
    size_t buf_size_used = buf_size;
    pepa_prebuf_t *pre = (pepa_prebuf_t *)buf;

    if (fd_in == core->sockets.shva_rw) {
        pepa_fill_prebuf(core, pre);
        buf_ptr += sizeof(pepa_prebuf_t);
        buf_size_used -= sizeof(pepa_prebuf_t);
    }

    do {
        /* Read one time, then we will transfer it possibly in pieces */
        rx_bytes += read(fd_in, buf_ptr + rx_bytes, buf_size_used - rx_bytes);
    } while (rx_bytes < 1 && EINTR == errno);

    if (rx_bytes < 1) {
        //if (do_debug) {
        slog_error_l("Could not read: from read sock %s (fd = %d): %s", name_in, fd_in, strerror(errno));
        //}
        return (-PEPA_ERR_BAD_SOCKET_READ);
    }

    pre->pepa_len = rx_bytes;
    return (rx_bytes + sizeof(pepa_prebuf_t));
}
#endif /* SEB */ /* 14/11/2024 */

__attribute__((hot))
#if 0 /* SEB */ /* 14/11/2024 */
static ssize_t pepa_socket_write(__attribute__((unused)) pepa_core_t *core,
                                 const int fd_out, const char *buf, const size_t rx_bytes,
                                 __attribute__((unused)) const int do_debug, const char *name_out){
    ssize_t rc;
    // ssize_t tx_bytes = 0;

    rc = send_exact(fd_out, buf, rx_bytes);
    if (rc < 0) {
        slog_error_l("Could not write: to write to the sock %s (fd = %d): %s", name_out, fd_out, strerror(errno));
        return (-PEPA_ERR_BAD_SOCKET_WRITE);
    }

    return (rc);
}
#endif /* SEB */ /* 14/11/2024 */

__attribute__((hot))
static ssize_t pepa_socket_write2(const int fd_out, const buf_and_header_t *bufh, const char *name_out)
{
    ssize_t rc = 0;
    TESTP_ASSERT(bufh, "bufh is NULL");
    TESTP_ASSERT(bufh->buf, "bufh->buf NULL");

    // ssize_t tx_bytes = 0;

    /* If prebuf in the structure is not NULL, send it with flag MSG_MORE */
    if (bufh->prebuf) {
        //slog_note_l("Add prebuf : fd out (FD = %d) %s", fd_out, pepa_detect_socket_name_by_fd(pepa_get_core(), fd_out));
        rc = send_exact_more(fd_out, (char *)bufh->prebuf, bufh->prebuf_size);
    } else {
        //slog_note_l("Do not add prebuf : fd out (FD = %d) %s", fd_out, pepa_detect_socket_name_by_fd(pepa_get_core(), fd_out));
    }

    if (rc < 0) {
        slog_error_l("Could not write prebuffer: to write to the sock %s (fd = %d): %s", name_out, fd_out, strerror(errno));
        return (-PEPA_ERR_BAD_SOCKET_WRITE);
    }

    /* Send the buffer */
    rc = send_exact(fd_out, bufh->buf, bufh->buf_used);
    if (rc < 0) {
        slog_error_l("Could not write: to write to the sock %s (fd = %d): %s", name_out, fd_out, strerror(errno));
        return (-PEPA_ERR_BAD_SOCKET_WRITE);
    }

    return (rc);
}

#if 0 /* SEB */ /* 14/11/2024 */
__attribute__((hot))static int pepa_is_all_transfered(const pepa_core_t *core, const size_t rx_bytes, const size_t tx_bytes, const int fd_out){
    size_t expected = rx_bytes;


    if (pepa_use_ticket(core, fd_out)) {
        expected += sizeof(pepa_prebuf_t);
    }

    if (tx_bytes > expected) {
        slog_fatal_l("Transfered more bytes than expected: %lu > %lu\n", tx_bytes, expected);
        abort();
    }

    if (expected == tx_bytes) {
        return (0);
    }

    return (1);
}
#endif /* SEB */ /* 14/11/2024 */

__attribute__((hot))
static int pepa_is_all_transfered2(const size_t rx_bytes, const size_t tx_bytes)
{
    if (tx_bytes > rx_bytes) {
        slog_fatal_l("Transfered more bytes than expected: %lu > %lu\n", tx_bytes, rx_bytes);
        abort();
    }

    if (rx_bytes == tx_bytes) {
        return (0);
    }

    return (1);
}

__attribute__((hot))
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
                             /* 11 */ const int max_iterations)
{

    int is_transfered = -1;
    int ret = PEPA_ERR_OK;
    int iteration = 0;

    TESTP_ASSERT(buf, "buf id  NULL");
    TESTP_ASSERT(name_in, "name_in id  NULL");
    TESTP_ASSERT(ext_rx, "ext_rx id  NULL");
    TESTP_ASSERT(ext_tx, "ext_tx id  NULL");

    memset(buf, 1, buf_size);

    if (do_debug) {
        slog_trace_l("Starrting transfering from %s to %s", name_in, name_out);
    }

    do {
        ssize_t tx_bytes = 0;

        iteration++;
        if (do_debug) {
            // llog_n("Iteration: %d", iteration);
        }

        /* This function adds a Ticket and PEPA ID, if they are enabled AND if the 'fd_out' is writing OUT socket */
        // pepa_add_id_and_ticket(core, buf, core->internal_buf_size, fd_out);

        buf_and_header_t *bufh = pepa_socket_read2(core, fd_in, buf, buf_size, name_in);
        if (NULL == bufh) {
            return (-PEPA_ERR_BAD_SOCKET_READ);
        }

        if (bufh->buf_used < 1) {
            buf_and_header_t_release(bufh);
            return (-PEPA_ERR_BAD_SOCKET_READ);
        }

        /* It is possible we recevived several messages;
         * the messages are divided by null terminator.
           We must print until the whole buffer is printed */


        // tx_bytes = pepa_socket_write(fd_out, buf, buf_size + offset, do_debug, name_out);

        pepa_set_fd_operations(core, fd_out, 2);
        tx_bytes = pepa_socket_write2(fd_out, bufh, name_out);
        pepa_set_fd_operations(core, fd_out, 0);
        usleep(1000);
        if (tx_bytes <= 0) {
            buf_and_header_t_release(bufh);
            ret = -PEPA_ERR_BAD_SOCKET_WRITE;
            goto endit;
        }

        if (core->dump_messages) {
            pepa_print_buffer2(core, bufh, fd_out, name_out, fd_in, name_in);
        }

        *ext_rx += (uint64_t)bufh->buf_used;
        *ext_tx += (uint64_t)tx_bytes;


        if (do_debug) {
            // llog_n("Iteration %d done: rx = %d, tx = %d", iteration, rx, tx);
        }

        /* Run this loop as long as we have data on read socket, but no more that max_iterations */

        is_transfered = pepa_is_all_transfered2(bufh->buf_used, tx_bytes);
        buf_and_header_t_release(bufh);

    } while (is_transfered != 0 && (iteration <= max_iterations));

    ret = PEPA_ERR_OK;
endit:
    if (do_debug) {
        // llog_n("Finished transfering from %s to %s, returning %d, rx = %d, tx = %d, ", name_in, name_out, ret, rx_total, tx_total);
    }


    // slog_note_l("Transfered [%ld -> %ld] bytes from %s (fd = %d) to %s (fd = %d)", rx_total, tx_total, name_in, fd_in, name_out, fd_out);

    return (ret);
}

#if 0 /* SEB */ /* 14/11/2024 */
__attribute__((hot))int pepa_one_direction_copy3(pepa_core_t *core,
                                                 const int fd_out, const char *name_out,
                                                 const int fd_in, const char *name_in,
                                                 const int do_debug,
                                                 uint64_t *ext_rx, uint64_t *ext_tx,
                                                 const int max_iterations){
    static char *buf = NULL;
    int is_transfered = -1;
    int ret = PEPA_ERR_OK;
    ssize_t rx_bytes = 0;
    ssize_t tx_total = 0;
    ssize_t rx_total = 0;
    int iteration = 0;

    if (NULL == buf) {
        buf = malloc(core->internal_buf_size);
        if (NULL == buf) {
            slog_error_l("Can not allocate internal buffer\n");
            abort();
        }
    }

    memset(buf, 0, core->internal_buf_size);

    if (do_debug) {
        slog_trace_l("Starrting transfering from %s to %s", name_in, name_out);
    }

    do {
        ssize_t tx_bytes = 0;

        iteration++;
        if (do_debug) {
            // llog_n("Iteration: %d", iteration);
        }

        /* This function adds a Ticket and PEPA ID, if they are enabled AND if the 'fd_out' is writing OUT socket */
        // pepa_add_id_and_ticket(core, buf, core->internal_buf_size, fd_out);

        rx_bytes = pepa_socket_read(core, iteration, fd_in, fd_out, buf, core->internal_buf_size, do_debug, name_in, name_out);

        if (rx_bytes <= 0) {
            ret = rx_bytes;
            goto endit;
        }

        /* It is possible we recevived several messages;
         * the messages are divided by null terminator.
           We must print until the whole buffer is printed */

        if (core->dump_messages) {
            pepa_print_buffer(core, buf, rx_bytes, fd_out, name_out, fd_in, name_in);
        }

        // tx_bytes = pepa_socket_write(fd_out, buf, buf_size + offset, do_debug, name_out);
        tx_bytes = pepa_socket_write(core, fd_out, buf, rx_bytes, do_debug, name_out);
        if (tx_bytes <= 0) {
            ret = -PEPA_ERR_BAD_SOCKET_WRITE;
            goto endit;
        }

        rx_total += rx_bytes;
        tx_total += tx_bytes;
        if (do_debug) {
            // llog_n("Iteration %d done: rx = %d, tx = %d", iteration, rx, tx);
        }

        /* Run this loop as long as we have data on read socket, but no more that max_iterations */

        is_transfered = pepa_is_all_transfered(core, rx_bytes, tx_bytes, fd_out);
    } while (is_transfered != 0 && (iteration <= max_iterations));

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

    // slog_note_l("Transfered [%ld -> %ld] bytes from %s (fd = %d) to %s (fd = %d)", rx_total, tx_total, name_in, fd_in, name_out, fd_out);

    return (ret);
}
#endif /* SEB */ /* 14/11/2024 */


int32_t pepa_test_fd(const int fd)
{
    if ((fcntl(fd, F_GETFL) < 0) && (EBADF == errno)) {
        return (-PEPA_ERR_FILE_DESCRIPTOR);
    }
    return (PEPA_ERR_OK);
}

int32_t epoll_ctl_add(const int epfd, const int sock, const uint32_t events)
{
    struct epoll_event ev;

    if (sock < 0) {
        slog_fatal_l("Tryed to add fd < 0: %d", sock);
        return (-PEPA_ERR_FILE_DESCRIPTOR);
    }

    ev.events = events;
    ev.data.fd = sock;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        slog_fatal_l("Can not add fd %d to epoll: %s", sock, strerror(errno));
        return (-PEPA_ERR_EPOLL_CANNOT_ADD);
    }

    return (PEPA_ERR_OK);
}

int32_t pepa_socket_shutdown_and_close(const int sock, const char *my_name)
{
    if (sock < 0) {
        slog_warn_l("%s: Looks like the socket is closed: == %d", my_name, sock);
        return (-PEPA_ERR_FILE_DESCRIPTOR);
    }

    int rc = shutdown(sock, SHUT_RDWR);
    if (rc < 0) {
        slog_warn_l("%s: Could not shutdown the socket: fd: %d, %s", my_name, sock, strerror(errno));
        return (-PEPA_ERR_FILE_DESCRIPTOR);
    }

    rc = close(sock);
    if (rc < 0) {
        slog_warn_l("%s: Could not close the socket: fd: %d, %s", my_name, sock, strerror(errno));
        return (-PEPA_ERR_CANNOT_CLOSE);
    }

    slog_note_l("%s: Closed socket successfully %d", my_name, sock);
    return (PEPA_ERR_OK);
}

void pepa_socket_close(const int socket, const char *socket_name)
{
    int rc;
    int i;
    if (socket < 0) {
        slog_note_l("Can not close socket %s, its value is %d: probaly it is closed", socket_name, socket);
        return;
    }

    /* Try to shutdown the socket before it closed */
    rc = shutdown(socket, SHUT_RDWR);
    if (rc < 0) {
        slog_note_l("%s: Could not shutdown the socket: fd: %d, %s", socket_name, socket, strerror(errno));
    }

    for (i = 0; i < 256; i++) {
        rc = close(socket);
        if (0 == rc) {
            slog_note_l("## Closed socket socket %s : %d, iterations: %d", socket_name, socket, i);
            return;
        }

        switch (errno) {
            /* If the error is BADF, we should do nothing, the fd is closed */
            case EBADF:
                return;
                /* The close() operation was interrupted; sleep and retry */
            case EINTR:
                usleep(100);
                continue;
            case EIO:
                slog_error_l("Can not close socket %s, error %d: %s, iterations: %d", socket_name, rc, strerror(errno), i);
                return;
            default:
                slog_error_l("Can not close socket %s, error %d: %s, iterations: %d", socket_name, rc, strerror(errno), i);
                return;
        }
    }
}

void pepa_reading_socket_close(const int socket, const char *socket_name)
{
    int i;
    char buf[16];
    int iterations = 0;
    ssize_t read_from = 0;

    if (socket < 0) {
        slog_note_l("Tried to close socket %s, its value is %d: probably is closed", socket_name, socket);
        return;
    }

    ssize_t rc = close(socket);
    if (0 == rc) {
        slog_note_l("## Closed from the first try socket socket %s, iterations: %d ", socket_name, iterations);
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
    slog_note_l("## Closed socket socket %s, iterations: %d ", socket_name, iterations);
}

void pepa_socket_close_in_listen(pepa_core_t *core)
{
    if (PEPA_ERR_OK != pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN")) {
        slog_warn_l("Close and shutdown of IN LISTEN is failed");
    }
    core->sockets.in_listen = FD_CLOSED;
    slog_note_l("Closed core->sockets.in_listen");
}

__attribute__((nonnull(1, 2)))
int pepa_open_listening_socket(struct sockaddr_in *s_addr,
                               const buf_t *ip_address,
                               const uint16_t port,
                               const int num_of_clients,
                               const char *name)
{
    int rc = PEPA_ERR_OK;
    int sock;

    // slog_debug_l("Open Socket [from %s]: starting for %s:%d, num of clients: %d", name, ip_address->data, port, num_of_clients);

    memset(s_addr, 0, sizeof(struct sockaddr_in));
    s_addr->sin_family = (sa_family_t)AF_INET;
    s_addr->sin_port = htons(port);

    // slog_debug_l("Open Socket [from %s]: Port is %d", name, port);

    const int inet_aton_rc = inet_aton(ip_address->data, &s_addr->sin_addr);

    // slog_note_l("Open Socket [from %s]: specified address, use %s", name, ip_address->data);
    /* Warning: inet_aton() returns 1 on success */
    if (1 != inet_aton_rc) {
        slog_fatal_l("Open Socket [from %s]: Could not convert string address to in_addr_t: %s", name, strerror(errno));
        return (-PEPA_ERR_CONVERT_ADDR);
    }

    // slog_note_l("Open Socket [from %s]: Going to create socket for %s:%d", name, ip_address->data, port);
    sock = socket(PF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0) {
        slog_error_l("Open Socket [from %s]: Could not create listen socket: %s", name, strerror(errno));
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    const int enable = 1;
    rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (rc < 0) {
        slog_error_l("Open Socket [from %s]: Could not set SO_REUSEADDR on socket, error: %s", name, strerror(errno));
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    // pepa_set_tcp_connection_props(core, sock);

    rc = bind(sock, (struct sockaddr *)s_addr, (socklen_t)sizeof(struct sockaddr_in));
    if (rc < 0 && EADDRINUSE == errno) {
        slog_warn_l("Address in use, can't bind, [from %s], waiting...", name);
        // sleep(10);
        return (-PEPA_ERR_SOCKET_BIND);
    }

    if (rc < 0) {
        slog_error_l("Open Socket [from %s]: Can't bind: %s", name, strerror(errno));
        close(sock);
        return (-PEPA_ERR_SOCKET_IN_USE);
    }

    rc = listen(sock, num_of_clients);
    if (rc < 0) {
        slog_fatal_l("Open Socket [from %s]: Could not set SERVER_CLIENTS: %s", name, strerror(errno));
        close(sock);
        return (-PEPA_ERR_SOCKET_LISTEN);
    }

#if 0 /* SEB */ /* 15/11/2024 */
    // slog_note_l("Open Socket [from %s]: Opened listening socket: %d", name, sock);
    rc = set_socket_blocking_mode(sock);
    if (rc) {
        slog_fatal_l("[%s] Can not set socket into blocking mode", name);
        abort();
    }

    slog_note_l("[%s] Set socket to blocking; now it is %s", name, utils_socked_blocking_or_not(sock));
#endif /* SEB */ /* 15/11/2024 */
    return (sock);
}

int pepa_open_connection_to_server(const char *address, const uint16_t port, const char *name)
{
    struct sockaddr_in s_addr;
    int sock;

    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family = (sa_family_t)AF_INET;

    slog_note_l("[from %s]: Converting addr string to binary: |%s|", name, address);
    const int convert_rc = inet_pton(AF_INET, address, &s_addr.sin_addr);
    if (0 == convert_rc) {
        slog_fatal_l("[from %s]: The string is not a valid IP address: |%s|", name, address);
        return (-PEPA_ERR_CONVERT_ADDR);
    }

    if (convert_rc < 0) {
        slog_fatal_l("[from %s]: Could not convert string addredd |%s| to binary", name, address);
        return (-PEPA_ERR_CONVERT_ADDR);
    }

    s_addr.sin_port = htons(port);

    slog_note_l("[from %s]: Creating socket: |%s|", name, address);
    if ((sock = socket(PF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) {
        slog_fatal_l("[from %s]: Could not create socket", name);
        // PEPA_TRY_ABORT();
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    slog_note_l("[from %s]: Starting connect(): |%s|", name, address);
    if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
        slog_note_l("[from %s]: Could not connect to server: %s", name, strerror(errno));
        close(sock);
        // PEPA_TRY_ABORT();
        return (-PEPA_ERR_SOCK_CONNECT);
    }

    slog_note_l("[from %s]: Opened connection to server: %d", name, sock);

    int rc = set_socket_blocking_mode(sock);
    if (rc) {
        slog_fatal_l("[%s] Can not set socket into blocking mode", name);
        abort();
    }

    usleep(10000);
    slog_note_l("[%s] Set socket to blocking; now it is %s", name, utils_socked_blocking_or_not(sock));
    return (sock);
}

int pepa_find_socket_port(const int sock)
{
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (sock < 0) {
        return (-1);
    }

    if (getsockname(sock, (struct sockaddr *)&sin, &len) == -1) {
        perror("getsockname");
        return (-1);
    } else {
        return (ntohs(sin.sin_port));
    }
}

