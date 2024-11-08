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
    slog_note("####################################################");
    slog_note_l("Thread %s: Detaching", name);
    if (0 != pthread_detach(pthread_self())) {
        slog_fatal_l("Thread %s: can't detach myself", name);
        perror("Thread : can't detach myself");
        return -PEPA_ERR_THREAD_DETOUCH;
    }

    slog_note_l("Thread %s: Detached", name);
    slog_note("####################################################");

    int32_t rc = pthread_setname_np(pthread_self(), name);
    if (0 != rc) {
        slog_fatal_l("Thread %s: can't set name", name);
    }

    return PEPA_ERR_OK;
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
    time_out.tv_sec = 1;
    time_out.tv_usec = 0;

    if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out))) {
        slog_debug_l("[from %s] SO_RCVTIMEO has a problem: %s", "EMU SHVA", strerror(errno));
    }
    if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&time_out, sizeof(time_out))) {
        slog_debug_l("[from %s] SO_SNDTIMEO has a problem: %s", "EMU SHVA", strerror(errno));
    }
}

void pepa_set_tcp_recv_size(const pepa_core_t *core, const int sock)
{
    uint32_t buf_size = core->internal_buf_size;

    /* Set TCP receive window size */
    if (0 != setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&buf_size, sizeof(buf_size))) {
        slog_debug_l("[from %s] SO_RCVBUF has a problem: %s", "EMU SHVA", strerror(errno));
    }
}

void pepa_set_tcp_send_size(const pepa_core_t *core, const int sock)
{
    uint32_t buf_size = core->internal_buf_size;
    /* Set TCP sent window size */
    if (0 != setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&buf_size, sizeof(buf_size))) {
        slog_debug_l("[from %s] SO_SNDBUF has a problem: %s", "EMU SHVA", strerror(errno));
    }
}

/**
 * @author Sebastian Mountaniol (20/10/2024)
 * @brief Test whether a Ticket should be used to buffer
 * @param pepa_core_t* core  PEPA Core structure
 * @param int fd_out         TX file descriptor
 * @return int               1 if a Ticket should be added, 0 otherwise
 * @details A Ticket should be added, if tickets eabled in the core structure (on the start it is parsed) and
 *  		if the fd_ou is OUT Write socket
 */
static int pepa_use_ticket(const pepa_core_t *core, const int fd_out)
{
    if (core->sockets.out_write == fd_out && core->use_ticket) {
        return 1;
    }
    return 0;
}

/**
 * @author Sebastian Mountaniol (20/10/2024)
 * @brief Test whether the PEPA ID should be used to buffer
 * @param pepa_core_t* core  PEPA Core structure
 * @param int fd_out         TX file descriptor
 * @return int               1 if the PEPA ID should be added, 0 otherwise
 * @details A PEPA ID should be added, if PEPA ID is eabled in the core structure (on the start it is parsed)
 *  		and if the fd_ou is OUT Write socket
 */
static int pepa_use_pepa_id(const pepa_core_t *core, const int fd_out)
{
    if (core->sockets.out_write == fd_out && core->use_id) {
        return 1;
    }
    return 0;
}


//#define DEB_BUF (128)
/* Print message */
static void pepa_print_buffer(pepa_core_t *core, char *buf, const ssize_t rx,
                              const int fd_out, const char *name_out,
                              const int fd_in, const char *name_in)
{
    // char    deb_buffer[DEB_BUF];
    /* offset: offset inside core->print_buffer*/
    size_t offset        = 0;
    /* in_buf_offset: offset inside the binary buffer we need to print out */
    size_t in_buf_offset = 0;

    int    prc           = 0; /** < return value of snprintf */

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
#if 0 /* SEB */ /* 30.09.2024 */
else {
        llog_w("Allocated printing buffer of size %u", core->print_buf_len);
    }
#endif /* SEB */ /* 30.09.2024 */

    //size_t min_len = ((DEB_BUF - 1) < rx) ? (DEB_BUF - 1) : rx;
    //memset(deb_buffer, 0, min_len);
    //memcpy(deb_buffer, buf, min_len);
    //printf("DEB: buf len = %ld, |%s|\n", rx, deb_buffer);

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
    for (ssize_t index = 0; index < (rx  - ((ssize_t)in_buf_offset)); index++) {
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

/**
 * 
 * @author se (9/24/24)
 * @brief This function adds a ticket and PEPA ID to the beginning of the buffer, if they are
 *  	  enabled and the destination is OUT 
 * @param core     Core structure
 * @param buf      Buffer allocated for sending
 * @param buf_size Size of the buffer
 * @param int fd_dst - File descriptor of the destination
 * @return size_t If a ticket and/or PEPA ID were added, the offset returned is the length of written ticket
 *  	   and/or PEPA ID. If they both are disabled, 0 will be returned.
 */
__attribute__((hot))
static void pepa_add_id_and_ticket(pepa_core_t *core, char *buf, const size_t buf_size, int fd_dst)
{
    size_t              offset = 0;
    static unsigned int ticket = 17;

    /* Add ticket and/or Pepa ID only to buffer dedicated to OUT socket */
    if (fd_dst != core->sockets.out_write) {
        return;
    }

    if (pepa_use_ticket(core, fd_dst)) {
        /* If the buffer is too short, we return 0 and do nothing */
        if (buf_size < offset + sizeof(pepa_ticket_t)) {
            printf("A wrong buf size: buf_size (%lu) < offset (%lu) + sizeof(unsigned int) (%lu)\n",
                   buf_size, offset, sizeof(pepa_ticket_t));
            sleep(1);
            abort();
        }

        ticket = pepa_gen_ticket(ticket);
        memcpy(buf + offset, &ticket, sizeof(pepa_ticket_t));
        offset += sizeof(pepa_ticket_t);
    }

    if (pepa_use_pepa_id(core, fd_dst)) {
        /* If the buffer is too short, we return the offset and do nothing */
        if (buf_size < offset + sizeof(pepa_id_t)) {
            // printf("A wrong buf size: buf_size (%lu) < offset (%lu) + sizeof(unsigned int) (%lu)\n", buf_size, offset, sizeof(unsigned int));
            slog_error_l("A wrong buf size: buf_size (%lu) < offset (%lu) + sizeof(unsigned int) (%lu)\n", buf_size, offset, sizeof(pepa_id_t));
            sleep(1);
            abort();
        }

        memcpy(buf + offset, &core->id_val, sizeof(pepa_id_t));
        offset += sizeof(pepa_id_t);
    }
}

static void peps_fill_prebuf(pepa_core_t *core, pepa_prebuf_t *pre)
{
    static unsigned int ticket = 17;

    pre->ticket = pepa_gen_ticket(ticket);
    pre->pepa_id  = core->id_val;
}

static ssize_t pepa_socket_read(pepa_core_t *core, int iteration, const int fd_in, const int fd_out,
                                char *buf, const size_t buf_size, const int do_debug,
                                const char *name_in, const char *name_out)
{
    char *buf_ptr = buf;
    ssize_t rx_bytes      = 0;
    size_t  offset        = 0;
    size_t  buf_size_used = buf_size;

    if (fd_in == core->sockets.shva_rw) {
        pepa_prebuf_t *pre = (pepa_prebuf_t *) buf;
        peps_fill_prebuf(core,  pre);
        buf_ptr += sizeof(pepa_prebuf_t);
        buf_size_used -= sizeof(pepa_prebuf_t);
    }

#if 0 /* SEB */ /* 02/11/2024 */
    if (pepa_use_ticket(core,  fd_out)) {
        offset += sizeof(unsigned int);
        buf_size_used -= sizeof(unsigned int);
    }

    if (pepa_use_pepa_id(core, fd_out)) {
        offset += sizeof(unsigned int);
        buf_size_used -= sizeof(unsigned int);
    }
#endif /* SEB */ /* 02/11/2024 */ 

    do {
        /* Read one time, then we will transfer it possibly in pieces */
        rx_bytes = read(fd_in, buf_ptr, buf_size_used);
    } while (rx_bytes < 1 && EINTR == errno);

    if (do_debug) {
        // llog_n("Iteration: %d, finised read(), there is %d bytes", iteration, rx);
    }

    if (rx_bytes < 0) {
        if (do_debug) {
            //llog_w("Could not read: from read sock %s [%d]: %s", name_in, fd_in, strerror(errno));
            slog_error_l("Could not read: from read sock %s [%d]: %s", name_in, fd_in, strerror(errno));
        }
        return -PEPA_ERR_BAD_SOCKET_READ;
    }

    /* nothing to read on the very first iteraion; it means, this socket is invalid */
    if ((0 == rx_bytes) && (1 == iteration)) {
        /* If we can not read on the first iteration, it probably means the fd was closed */
        slog_error_l("Could not read on the first iteration: from read sock %s [%d] out socket %s [%d]: %s", name_in, fd_in, name_out, fd_out, strerror(errno));
        return -PEPA_ERR_BAD_SOCKET_READ;
    }
    return rx_bytes;
}

static ssize_t send_exact(const int sock_fd, const char *buf, const size_t num_bytes)
{
    size_t total_sent = 0;
    while (total_sent < num_bytes) {
        ssize_t bytes_sent = send(sock_fd, buf + total_sent, num_bytes - total_sent, 0);

        if (bytes_sent < 1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, try again
            } else {

                slog_error_l("Error while sending: sent %lu bytes, expected to send %lu, error is: %s", total_sent, num_bytes, strerror(errno));
                return -1;  // Error occurred
            }
        }
        total_sent += bytes_sent;
    }
    return total_sent;
}

__attribute__((hot))
static ssize_t pepa_socket_write(pepa_core_t *core, const int fd_out, char *buf, const size_t rx_bytes,
                                 const int do_debug, const char *name_out)
{
    ssize_t rc;
    size_t  buf_size_used = rx_bytes;
    ssize_t tx_bytes      = 0;
#if 0 /* SEB */ /* 02/11/2024 */

    if (fd_out == core->sockets.out_write) {
        pepa_prebuf_t pre;
        peps_fill_prebuf(core,  &pre);

        rc = send_exact(fd_out, (char *) &pre, sizeof(pepa_prebuf_t));
        if (rc != sizeof(pepa_prebuf_t)) {
            return -PEPA_ERR_BAD_SOCKET_WRITE;
        }
    }
#endif /* SEB */ /* 02/11/2024 */

    rc = send_exact(fd_out, buf, rx_bytes);
    if (rc != (ssize_t) rx_bytes) {
        return -PEPA_ERR_BAD_SOCKET_WRITE;
    }
    tx_bytes += rc;

#if 0 /* SEB */ /* 02/11/2024 */
    do {
        ssize_t tx_current = 0;
        do {
            tx_current  = write(fd_out, &pre + tx_current, sizeof(pepa_prebuf_t) - tx_current);
        } while (tx_current < 1 && EINTR == errno);

        if (tx_current <= 0) {
            if (do_debug) {
                // llog_w("Could not write to write to sock %s [%d]: returned < 0: %s", name_out, fd_out, strerror(errno));
                slog_warn_l("Could not write to write to sock %s [%d]: returned < 0: %s", name_out, fd_out, strerror(errno));
            }
            return -PEPA_ERR_BAD_SOCKET_WRITE;
        }

        tx_bytes += tx_current;

        /* If we still not transfered everything, give the TX socket some time to rest and finish the transfer */
        /* NOTE: The conversion of tx_byte to unsigned is safe here since we tested abive that returning status > 0 */
        if ((size_t)tx_bytes < buf_size_used) {
            usleep(10);
        }

    } while ((size_t)tx_bytes < buf_size_used);
#endif /* SEB */ /* 02/11/2024 */



/* Write until the whole received buffer was transferred */
    do {
        ssize_t tx_current = 0;
        do {
            tx_current  = write(fd_out, buf + tx_bytes, buf_size_used - tx_bytes);
        } while (tx_current < 1 && EINTR == errno);

        if (tx_current <= 0) {
            if (do_debug) {
                // llog_w("Could not write to write to sock %s [%d]: returned < 0: %s", name_out, fd_out, strerror(errno));
                slog_warn_l("Could not write to write to sock %s [%d]: returned < 0: %s", name_out, fd_out, strerror(errno));
            }
            return -PEPA_ERR_BAD_SOCKET_WRITE;
        }

        tx_bytes += tx_current;

        /* If we still not transfered everything, give the TX socket some time to rest and finish the transfer */
        /* NOTE: The conversion of tx_byte to unsigned is safe here since we tested abive that returning status > 0 */
        if ((size_t)tx_bytes < buf_size_used) {
            usleep(10);
        }

    } while ((size_t)tx_bytes < buf_size_used);
    return tx_bytes;
}

__attribute__((hot))
static int pepa_is_all_transfered(const pepa_core_t *core, const size_t rx_bytes, const size_t tx_bytes, const int fd_out)
{
    size_t expected = rx_bytes;

#if 0 /* SEB */ /* 02/11/2024 */
    if (pepa_use_ticket(core, fd_out)) {
        expected += sizeof(unsigned int);
    }

    if (pepa_use_ticket(core, fd_out)) {
        expected += sizeof(unsigned int);
    }
#endif /* SEB */ /* 02/11/2024 */ 

    if (pepa_use_ticket(core, fd_out)) {
        expected += sizeof(pepa_prebuf_t);
    }

    if (tx_bytes > expected) {
        printf("Transfered more bytes than expected: %lu > %lu\n", tx_bytes, expected);
        abort();
    }

    if (expected == tx_bytes) {
        return 0;
    }

    return 1;
}

__attribute__((hot))
int pepa_one_direction_copy3(pepa_core_t *core,
                             const int fd_out, const char *name_out,
                             const int fd_in, const char *name_in,
                             const int do_debug,
                             uint64_t *ext_rx, uint64_t *ext_tx,
                             const int max_iterations)
{
    static char *buf          = NULL;
    int         is_transfered = -1;
    int         ret           = PEPA_ERR_OK;
    ssize_t     rx_bytes      = 0;
    ssize_t     tx_total      = 0;
    ssize_t     rx_total      = 0;
    int         iteration     = 0;

    if (NULL == buf) {
        buf = malloc(core->internal_buf_size);
        if (NULL == buf) {
            slog_error_l("Can not allocate internal buffer\n");
            abort();
        }
    }

    memset(buf, 1, core->internal_buf_size);

    if (do_debug) {
        // llog_n("Starrting transfering from %s to %s", name_in, name_out);
    }

    do {
        ssize_t tx_bytes   = 0;

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

    return ret;
}


#if 0 /* SEB */
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
        slog_warn_l("Allocated printing buffer");
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

        slog_note("%s", core->print_buf);

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
        slog_error_l("Can not allocate printing buffer, asked size is: %u; message was not printed", core->print_buf_len);
        return;
    } else {
        slog_warn_l("Allocated printing buffer of size %u", core->print_buf_len);
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
        slog_debug("%s", core->print_buf);

        if (offset + 1 < rx) {
            offset = find_next(buf, rx, offset + 1, 0);
        }
        offset++;
        mes_num++;
    } while (offset < rx);
}
#endif

#if 0 /* SEB */ /* 15.10.2024 */
/* Print message */
static void pepa_print_buffer(pepa_core_t *core, char *buf, const ssize_t rx,
                              const int fd_out, const char *name_out,
                              const int fd_in, const char *name_in){
    ssize_t offset  = 0;
    int     prc     = 0; /** < return value of snprintf */

    /* On the first message we allocate a buffer for printing */
    if (NULL == core->print_buf) {
        core->print_buf = calloc(core->print_buf_len, 1);
    }

    if (NULL == core->print_buf) {
        slog_error_l("Can not allocate printing buffer, asked size is: %u; message was not printed", core->print_buf_len);
        return;
    } else {
        slog_warn_l("Allocated printing buffer of size %u", core->print_buf_len);
    }

    /* Print header */
    offset = snprintf(core->print_buf, core->print_buf_len, "+++ MES: %s [fd:%.2d] -> %s [fd:%.2d], LEN:%zd |",
                      /* mes num */ name_in, fd_in, name_out, fd_out, rx);

    for (int index = 0; index < rx; index++) {
        if (isprint(buf[index])) {
            core->print_buf[offset] = buf[index];
            offset++;

        } else {
            prc = snprintf(core->print_buf + offset, core->print_buf_len - offset, "<0X%X>", (unsigned int) buf[index]);
            if (prc < 1) {
                slog_error_l("Can not print non-printable character from, offset of the character: %d; stopped printing", index);
                return;
            }

            offset += prc;
        }
    }

    core->print_buf[offset] = '|';
    offset++;
    core->print_buf[offset] = '\0';

    /* Print */
    slog_debug("%s", core->print_buf);
}

__attribute__((hot))
int pepa_one_direction_copy3(pepa_core_t *core,
                             const int fd_out, const char *name_out,
                             const int fd_in, const char *name_in,
                             char *buf, const size_t buf_size,
                             const int do_debug,
                             uint64_t *ext_rx, uint64_t *ext_tx,
                             const int max_iterations){
    //static char *print_buf   = calloc(core->internal_buf_size + 1, 1);

    int     ret          = PEPA_ERR_OK;
    ssize_t rx           = 0;
    ssize_t tx_total     = 0;
    ssize_t rx_total     = 0;
    int     iteration    = 0;
    size_t  buf_size_use = buf_size;

    /* If message dump is enabled, keep 1 character for \0 */
    if (core->dump_messages) {
        buf_size_use--;
    }

    if (do_debug) {
        // slog_note_l("Starrting transfering from %s to %s", name_in, name_out);
    }

    do {
        ssize_t tx         = 0;
        ssize_t tx_current = 0;

        iteration++;
        if (do_debug) {
            // slog_note_l("Iteration: %d", iteration);
        }

        /* Read one time, then we will transfer it possibly in pieces */
        rx = read(fd_in, buf, buf_size_use);

        if (do_debug) {
            // slog_note_l("Iteration: %d, finised read(), there is %d bytes", iteration, rx);
        }


        if (rx < 0) {
            if (do_debug) {
                slog_warn_l("Could not read: from read sock %s [%d]: %s", name_in, fd_in, strerror(errno));
            }
            ret = -PEPA_ERR_BAD_SOCKET_READ;
            goto endit;
        }

        /* nothing to read on the furst iteraion; it means, this socket is invalid */
        if ((0 == rx) && (1 == iteration)) {
            /* If we can not read on the first iteration, it probably means the fd was closed */
            ret = -PEPA_ERR_BAD_SOCKET_READ;

            if (do_debug) {
                slog_error_l("Could not read on the first iteration: from read sock %s [%d] out socket %s [%d}: %s",
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
            pepa_print_buffer(core, buf, rx, fd_out, name_out, fd_in, name_in);
        }

        /* Write until transfer the whole received buffer */
        do {
            tx_current  = write(fd_out, buf, (size_t)(rx - tx));

            if (tx_current <= 0) {
                ret = -PEPA_ERR_BAD_SOCKET_WRITE;
                if (do_debug) {
                    slog_warn_l("Could not write to write to sock %s [%d]: returned < 0: %s", name_out, fd_out, strerror(errno));
                }
                goto endit;
            }

            tx += tx_current;

            /* If we still not transfered everything, give the TX socket some time to rest and finish the transfer */
            if (tx < rx) {
                usleep(10);
            }

        } while (tx < rx);

        rx_total += rx;
        tx_total += tx;
        if (do_debug) {
            // slog_note_l("Iteration %d done: rx = %d, tx = %d", iteration, rx, tx);
        }

        /* Run this loop as long as we have data on read socket, nut no more that max_iterations */
    } while (((int32_t)buf_size_use == rx) && (iteration <= max_iterations));

    ret = PEPA_ERR_OK;
    endit:
    if (do_debug) {
        // slog_note_l("Finished transfering from %s to %s, returning %d, rx = %d, tx = %d, ", name_in, name_out, ret, rx_total, tx_total);
    }

    if (rx_total > 0) {
        *ext_rx += (uint64_t)rx_total;

    }

    if (tx_total > 0) {
        *ext_tx += (uint64_t)tx_total;

    }

    return ret;
}
#endif /* SEB */ /* 15.10.2024 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <stdlib.h>

#define HEADER_BUF_LEN (sizeof(pepa_ticket_t) + sizeof (pepa_id_t))
#define PEPA_ERR_BAD_SOCKET_WRITE -1
#define PEPA_ERR_BAD_SOCKET_READ -2
#define PEPA_ERR_BAD_SOCKET_OTHER -3
#define PEPA_ERR_DATA_TRUNCATION -4

// Predefined header buffer
const char HEADER_BUF[HEADER_BUF_LEN] = "12345678";

/**
 * Reads available data from fd_in up to BUF_SIZE bytes into buf.
 * Returns the number of bytes read, or a negative error code on failure.
 */
static ssize_t read_data(pepa_core_t *core, int fd_in, char *buf)
{
    ssize_t bytes_read = 0;

    while (1) {
        ssize_t result = read(fd_in, buf + bytes_read, core->internal_buf_size - bytes_read);

        if (result < 0) {
            if (errno == ECONNRESET) {
                return PEPA_ERR_BAD_SOCKET_READ;  // Broken read socket
            } else if (errno == EINTR) {
                // Interrupted: continue to attempt reading
                continue;
            } else {
                return PEPA_ERR_BAD_SOCKET_OTHER;  // Other errors
            }
        } else if (result == 0) {
            // End of stream reached
            break;
        }

        bytes_read += result;

        // If we have filled the buffer, break out of the loop
        if (bytes_read >= core->internal_buf_size) {
            break;  // We can't read more than BUF_SIZE
        }
    }

    return bytes_read;  // Return the total number of bytes read
}

/**
 * Sends the header and then the data from buf to fd_out.
 * Returns 0 on success, or a negative error code on failure.
 */
static int send_data(int fd_out, const char *header_buf, size_t header_len, const char *buf, size_t bytes_to_write)
{
    if (header_buf != NULL && bytes_to_write > 0) {
        // Send header first with MSG_MORE flag
        size_t     header_bytes_left = header_len;
        const char *header_ptr       = header_buf;
        while (header_bytes_left > 0) {
            //ssize_t header_bytes_written = send(fd_out, header_ptr, header_bytes_left, MSG_MORE);
            ssize_t header_bytes_written = write(fd_out, header_ptr, header_bytes_left);
            if (header_bytes_written < 0) {
                if (errno == EPIPE) {
                    return PEPA_ERR_BAD_SOCKET_WRITE;  // Broken write socket
                } else if (errno == EINTR) {
                    continue;  // Interrupted: retry send
                } else {
                    return PEPA_ERR_BAD_SOCKET_OTHER;  // Other errors
                }
            }
            header_bytes_left -= header_bytes_written;
            header_ptr += header_bytes_written;
        }
    }

    // Send main buffer data
    size_t     bytes_left = bytes_to_write;
    const char *buf_ptr   = buf;
    while (bytes_left > 0) {
        ssize_t bytes_written = write(fd_out, buf_ptr, bytes_left);
        if (bytes_written < 0) {
            if (errno == EPIPE) {
                return PEPA_ERR_BAD_SOCKET_WRITE;  // Broken write socket
            } else if (errno == EINTR) {
                continue;  // Interrupted: retry send
            } else {
                return PEPA_ERR_BAD_SOCKET_OTHER;  // Other errors
            }
        }
        bytes_left -= bytes_written;
        buf_ptr += bytes_written;
    }

    return 0;
}

/**
 * Transfers available data from fd_in to fd_out up to max_iterations times.
 * If add_header is non-null, sends HEADER_BUF at the beginning of each transfer.
 * Updates ext_rx and ext_tx with the total bytes read and written respectively.
 * Returns 0 on success or a negative error code on failure.
 */
int transfer_data4(pepa_core_t *core,
                   const int fd_out, const char *name_out,
                   const int fd_in, const char *name_in,
                   const int do_debug,
                   uint64_t *ext_rx, uint64_t *ext_tx,
                   const int max_iterations)
{
    char *header_buf = NULL;
    char *buf        = malloc(core->internal_buf_size);

    if (pepa_use_ticket(core, fd_out)) {
        header_buf = malloc(HEADER_BUF_LEN);
        memset(header_buf, 8, HEADER_BUF_LEN);
    }

    if (buf == NULL) {
        return PEPA_ERR_BAD_SOCKET_OTHER;  // Memory allocation failure
    }
    memset(buf, '2', core->internal_buf_size);  // Fill buffer with '2's

    for (int i = 0; i < max_iterations; ++i) {
        // Read available data from fd_in
        ssize_t bytes_read = read_data(core, fd_in, buf);
        if (bytes_read < 0) {
            free(buf);
            return bytes_read;  // Return error code from read_data
        } else if (bytes_read == 0) {
            // End of input stream
            free(buf);
            return 0;
        }

        // Update received bytes counter
        *ext_rx += bytes_read;

        // Send the header and data to fd_out if add_header is not null
        int send_status = send_data(fd_out, header_buf ? HEADER_BUF : NULL, HEADER_BUF_LEN, buf, bytes_read);
        if (send_status < 0) {
            free(buf);
            return send_status;  // Return error code from send_data
        }

        // Update sent bytes counter
        *ext_tx += bytes_read + (header_buf ? HEADER_BUF_LEN : 0);  // Include header length if applicable
    }

    free(buf);  // Free allocated buffer
    return 0;  // Success
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
        slog_fatal_l("Tryed to add fd < 0: %d", sock);
        return -PEPA_ERR_FILE_DESCRIPTOR;
    }

    ev.events = events;
    ev.data.fd = sock;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        slog_fatal_l("Can not add fd %d to epoll: %s", sock, strerror(errno));
        return -PEPA_ERR_EPOLL_CANNOT_ADD;
    }

    return PEPA_ERR_OK;
}

int32_t pepa_socket_shutdown_and_close(const int sock, const char *my_name)
{
    if (sock < 0) {
        slog_warn_l("%s: Looks like the socket is closed: == %d", my_name, sock);
        return -PEPA_ERR_FILE_DESCRIPTOR;
    }

    int rc = shutdown(sock, SHUT_RDWR);
    if (rc < 0) {
        slog_warn_l("%s: Could not shutdown the socket: fd: %d, %s", my_name, sock, strerror(errno));
        return -PEPA_ERR_FILE_DESCRIPTOR;
    }

    rc = close(sock);
    if (rc < 0) {
        slog_warn_l("%s: Could not close the socket: fd: %d, %s", my_name, sock, strerror(errno));
        return -PEPA_ERR_CANNOT_CLOSE;
    }

    slog_note_l("%s: Closed socket successfully %d", my_name, sock);
    return PEPA_ERR_OK;
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
    int     i;
    char    buf[16];
    int     iterations = 0;
    ssize_t read_from  = 0;

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
    int rc   = PEPA_ERR_OK;
    int sock;

    //slog_debug_l("Open Socket [from %s]: starting for %s:%d, num of clients: %d", name, ip_address->data, port, num_of_clients);

    memset(s_addr, 0, sizeof(struct sockaddr_in));
    s_addr->sin_family = (sa_family_t)AF_INET;
    s_addr->sin_port = htons(port);

    //slog_debug_l("Open Socket [from %s]: Port is %d", name, port);

    const int inet_aton_rc = inet_aton(ip_address->data, &s_addr->sin_addr);

    //slog_note_l("Open Socket [from %s]: specified address, use %s", name, ip_address->data);
    /* Warning: inet_aton() returns 1 on success */
    if (1 != inet_aton_rc) {
        slog_fatal_l("Open Socket [from %s]: Could not convert string address to in_addr_t: %s", name, strerror(errno));
        return (-PEPA_ERR_CONVERT_ADDR);
    }

    // slog_note_l("Open Socket [from %s]: Going to create socket for %s:%d", name, ip_address->data, port);
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock  < 0) {
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
        return -PEPA_ERR_SOCKET_BIND;
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

    //slog_note_l("Open Socket [from %s]: Opened listening socket: %d", name, sock);
    return (sock);
}

int pepa_open_connection_to_server(const char *address, const uint16_t port, const char *name)
{
    struct sockaddr_in s_addr;
    int                sock;

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
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        slog_fatal_l("[from %s]: Could not create socket", name);
        //PEPA_TRY_ABORT();
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    slog_note_l("[from %s]: Starting connect(): |%s|", name, address);
    if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
        slog_note_l("[from %s]: Could not connect to server: %s", name, strerror(errno));
        close(sock);
        //PEPA_TRY_ABORT();
        return (-PEPA_ERR_SOCK_CONNECT);
    }

    slog_note_l("[from %s]: Opened connection to server: %d", name, sock);
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

