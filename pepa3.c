#define _GNU_SOURCE
#include "pepa3.h"

#include <errno.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "pepa_config.h"
#include "pepa_core.h"
#include "pepa_errors.h"
#include "pepa_in_reading_sockets.h"
#include "pepa_socket_common.h"
#include "pepa_state_machine.h"
#include "pepa_utils.h"
#include "queue.h"
#include "slog/src/slog.h"

/**
 * @author Sebastian Mountaniol (1/9/24)
 * @brief PEPA state machine states
 * @details
 */
enum pepa3_go_states {
    PST_START = 1000,  /**< Start state, executed  once  */
    PST_CLOSE_SOCKETS, /**< All sockets must be closed */
    PST_RESET_SOCKETS, /**< Only reading sockets should be closed, listeners stay */
    PST_WAIT_OUT,      /**< Wit OUT connection */
    PST_OPEN_SHVA,     /**< Connect to SHVA server */
    PST_START_IN,      /**< Start IN listening socket */
    PST_WAIT_IN,       /**< Start IN listening socket */
    PST_RESTART_IN,    /**< IN listening socket reuires full restart */
    PST_TRANSFER_LOOP, /**< Start transfering loop */
    PST_END,           /**< PEPA must exit */
};

/**
 * @author Sebastian Mountaniol (1/9/24)
 * @brief Internal statuses
 * @details
 */
enum pepa3_errors {
    TE_RESTART = 3000, /**< All sockets need full restart */
    TE_RESET,          /**< Sockets need partial restart, listening sockets are OK */
    TE_IN_RESTART,     /**< IN sockets need full restart */
    TE_IN_REMOVED,     /**< On of IN listening sockets was removed */
};

/* This queue keeps requesst to read from SHVA and send to OUT */
queue_t *q_shva = NULL;
queue_t *q_in = NULL;

static void *q_get(queue_t *q)
{
    return queue_pop_right(q);
}

static int q_put(queue_t *q, void *buf)
{
    return queue_push_left(q, buf);
}

static void init_queues(void)
{
    q_shva = queue_create();
    if (NULL == q_shva) {
        slog_error("Can note init printing queue!");
        abort();
    }

    q_in = queue_create();
    if (NULL == q_in) {
        slog_error("Can note init printing queue!");
        abort();
    }
    return 0;
}

static void pepa_remove_socket_from_epoll(pepa_core_t *core, const int fd, const char *fd_name, const char *file, const int line)
{
    /* Remove the broken IN read socket from the epoll */
    int rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, fd, NULL);

    if (rc_remove) {
        switch (errno) {
            case ENOENT:
                slog_warn_l("Can not remove from EPOLL: The socket [%s] [%d] (%s +%d) is already removed from the epoll set", fd_name, fd, file, line);
                break;
            case EBADF:
                slog_warn_l("Can not remove from EPOLL: The socket [%s] [%d] (%s +%d) is a bad file descriptor", fd_name, fd, file, line);
                break;
            default:
                slog_warn_l("Can not remove from EPOLL: The socket [%s] [%d] (%s +%d) can not be removed, the error is: %s", fd_name, fd, file, line, strerror(errno));
        }
    }
}

/**
 * @author Sebastian Mountaniol (20/10/2024)
 * @brief Test epoll events for one file descriptor. If there is an error on the socket (closed, any other
 *        error), this function indicates it
 * @param uint32_t events Events for one file descriptor
 * @return int 1 if there a problem with the socket, PEPA_ERR_OK if no errors
 * @details
 */
static int pepa_epoll_fd_ok(uint32_t events)
{
    if (0 != (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
        return 1;
    }
    return PEPA_ERR_OK;
}

/* Max number of events we acccept from epoll */
#define EVENTS_NUM (100)

/* epoll max timeout, milliseconds */
#define EPOLL_TIMEOUT (100)

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Process exceptions on all sockets
 * @param pepa_core_t* core       Core structure
 * @param const struct epoll_event[] events     Events of epoll
 * @param const int event_count Number of epoll events
 * @return int PEPA_ERR_OK on success, ot the next state machine
 *  	   status otherwise
 * @details
 */
static int pepa_process_exceptions(pepa_core_t *core, const struct epoll_event events_array[], const int event_count)
{
    int ret = PEPA_ERR_OK;
    // int rc_remove;
    int i;
    for (i = 0; i < event_count; i++) {
        uint32_t events = events_array[i].events;
        int fd = events_array[i].data.fd;

        // if (!(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
        if (PEPA_ERR_OK == pepa_epoll_fd_ok(events)) {
            continue;
        }

        /*** The remote side is disconnected ***/

        /* If one of the read/write sockets is diconnected, stop */
        if (events & (EPOLLRDHUP)) {
            /* SHVA reading socket is disconnected */
            if (core->sockets.shva_rw == fd) {
                slog_warn_l("SHVA socket: remote side of the socket is disconnected");
                return TE_RESTART;
            }

            /* OUT writing socket is disconnected */
            if (core->sockets.out_write == fd) {
                slog_warn_l("OUT socket: remote side of the OUT write socket is disconnected");
                return TE_RESTART;
            }

            /* OUT listener socket is disconnected */
            if (core->sockets.out_listen == fd) {
                slog_warn_l("OUT socket: remote side of the OUT listen is disconnected");
                return TE_RESTART;
            }

            /* Else: it is one of IN reading sockets, we should remove it */
            pepa_remove_socket_from_epoll(core, fd, "IN", __FILE__, __LINE__);
            pepa_in_reading_sockets_close_rm(core, fd);
            ret = TE_IN_REMOVED;

        } /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) */

        /*** This side is broken ***/

        if (events & (EPOLLHUP)) {
            /* SHVA reading socket is disconnected */
            if (core->sockets.shva_rw == fd) {
                slog_warn_l("SHVA socket: local side of the socket is broken");
                return TE_RESTART;
            }

            /* OUT writing socket is disconnected */
            if (core->sockets.out_write == fd) {
                slog_warn_l("OUT socket: local side of the OUT write socket is broken");
                return TE_RESTART;
            }

            /* OUT listener socket is disconnected */
            if (core->sockets.out_listen == fd) {
                slog_warn_l("OUT socket: local side of the OUT listen is broken");
                return TE_RESTART;
            }

            /* IN listener socket is degraded */
            if (core->sockets.in_listen == fd) {
                slog_warn_l("IN socket: local side of the IN listen is broken");
                return TE_IN_RESTART;
            }

            /* Else: it is one of IN reading sockets, we should remove it */
            pepa_remove_socket_from_epoll(core, fd, "IN", __FILE__, __LINE__);
            pepa_in_reading_sockets_close_rm(core, fd);
            ret = TE_IN_REMOVED;

        } /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) */
    }
    return ret;
}

/**
 * @author Sebastian Mountaniol (1/7/24)
 * @brief This function is called when IN socket should accept a
 *  	  new connection. This function calls accept() and adds
 *  	  a new socket into epoll set, and into the array of IN
 *  	  reading sockets
 * @param pepa_core_t* core  Pepa core structure
 * @return int32_t PEPA_ERR_OK on success; an error if accept failed
 * @details
 */
static int32_t pepa_in_accept_new_connection(pepa_core_t *core)
{
    struct sockaddr_in address;
    int32_t new_socket = FD_CLOSED;
    static int32_t addrlen = sizeof(address);

    if ((new_socket = accept(core->sockets.in_listen,
                             (struct sockaddr *)&address,
                             (socklen_t *)&addrlen)) < 0) {
        slog_error_l("Error on accept: %s", strerror(errno));
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    pepa_set_tcp_timeout(new_socket);
    pepa_set_tcp_recv_size(core, new_socket, "IN READ");
    const int enable = 1;
    int rc = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (rc < 0) {
        slog_error_l("Open Socket: Could not set SO_REUSEADDR on socket, error: %s", strerror(errno));
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    rc = set_socket_blocking_mode(new_socket);
    if (rc) {
        slog_fatal_l("Can not set socket into blocking mode");
        abort();
    }

    if (0 != epoll_ctl_add(core->epoll_fd, new_socket, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add new socket to epoll set: %s", strerror(errno));
        pepa_reading_socket_close(new_socket, "IN FORWARD-READ");
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    /* Add to the array of IN reading sockets */
    pepa_in_reading_sockets_add(core, new_socket);

    slog_warn_l("Added new socket %d to epoll set", new_socket);
    return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Process only SHVA incoming buffers
 * @param pepa_core_t* core       Core structure
 * @param const struct epoll_event[] events     Event returned
 *  			from epoll_wait()
 * @param const int event_count Number of events
 * @return int PEPA_ERR_OK on success, a negative error code
 *  	   otherwise
 * @details We always process SHVA sockets first; if there are
 *  		multiple and overloaded IN sockets,
 *  		the SHVA processing cab be degraded
 */
#if 1 /* SEB */ /* 13/11/2024 */
static int pepa_process_fdx_shva(pepa_core_t *core, const struct epoll_event events_array[], const int event_count)
{
    int32_t rc = PEPA_ERR_OK;
    int32_t i;

    for (i = 0; i < event_count; i++) {
        uint32_t events = events_array[i].events;
        int fd = events_array[i].data.fd;

        if (core->sockets.shva_rw != fd) {
            continue;
        }

        if (!(events & EPOLLIN)) {
            continue;
        }

        /* Read /write from/to socket */

        // transfer_data4
        // rc = pepa_one_direction_copy3(core,
        rc = pepa_one_direction_copy4(core,
                                      core->buffer, core->internal_buf_size,
                                      // rc = transfer_data4(core,
                                      /* Send to : */ core->sockets.out_write, "OUT",
                                      /* From: */ core->sockets.shva_rw, "SHVA",
                                      /*Debug is ON */ 0,
                                      /* RX stat */ &core->monitor.shva_rx,
                                      /* TX stat */ &core->monitor.out_tx,
                                      /* Max iterations */ /*5*/ 1);

        if (PEPA_ERR_OK == rc) {
            // slog_warn_l("%s: Sent from socket %d", "IN-FORWARD", events[i].data.fd);
            return PEPA_ERR_OK;
        }

        slog_note_l("An error on sending buffers from SHVA to OUT: %s", pepa_error_code_to_str(rc));

        /* Something wrong with the socket, should be removed */

        /* Writing side is off, means: SHVA or OUT socket is invalid */
        /* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
        if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
            slog_note_l("Could not write to %s; setting system to FAIL", "OUT");
        }

        if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
            /* Here are two cases: the read can be IN or SHVA. IN case of SHVA we must restart all sockets */
            slog_note_l("Could not read from %s; setting system to FAIL", "SHVA");
        }

        return TE_RESTART;
    }
    return PEPA_ERR_OK;
}
#endif /* SEB */ /* 13/11/2024 */

static int pepa_process_fdx_analyze_error(const int ev, const char *name)
{
    if (ev & EPOLLRDHUP) {
        slog_error_l("[%s] Remote side disconnected", name);
        return -PEPA_ERR_BAD_SOCKET_REMOTE;
    }

    /* This side is broken. Close and remove this file descriptor from IN, return error */
    if (ev & EPOLLHUP) {
        slog_error_l("[%s] This side disconnected", name);
        return -PEPA_ERR_BAD_SOCKET_LOCAL;
    }

    /* Another error happened. Close and remove this file descriptor from IN, return error*/
    if (ev & EPOLLERR) {
        slog_error_l("[%s] Unknown error", name);
        return -PEPA_ERR_BAD_SOCKET_LOCAL;
    }

    return PEPA_ERR_OK;
}

typedef struct {
    int64_t cnt;
    ;
    pepa_core_t *core;
    int fd_in;
    int fd_out;
    char *name_in;
    char *name_out;
    uint64_t *rx_stat;
    uint64_t *tx_stat;
} precessing_req_t;

int64_t g_counter = 100;

/* The counter of SHVA incoming */
int64_t g_processed_counter_shva = 0;

/* The counter of INT incoming */
int64_t g_processed_counter_in = 0;

pthread_mutex_t g_shva_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t g_processed_counter_shva_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_processed_counter_in_lock = PTHREAD_MUTEX_INITIALIZER;

/* 0 = shva socked is not processed, 1 = dhva socket is processed */
int shva_processing_st = 0;
pthread_mutex_t shva_processing_st_lock = PTHREAD_MUTEX_INITIALIZER;

int thread_reponce = 0;
pthread_mutex_t thread_reponce_lock = PTHREAD_MUTEX_INITIALIZER;

static void set_thread_response(int st)
{
    pthread_mutex_lock(&thread_reponce_lock);
    thread_reponce = st;
    pthread_mutex_unlock(&thread_reponce_lock);
}

static int get_thread_response(void)
{
    int rc;
    pthread_mutex_lock(&thread_reponce_lock);
    rc = thread_reponce;
    thread_reponce = 0;
    pthread_mutex_unlock(&thread_reponce_lock);
    return rc;
}

static void set_processed_counter_shva(uint64_t cnt)
{
    pthread_mutex_lock(&g_processed_counter_shva_lock);
    g_processed_counter_shva = cnt;
    pthread_mutex_unlock(&g_processed_counter_shva_lock);
}

static int64_t get_processed_counter_shva(void)
{
    uint64_t rc;
    pthread_mutex_lock(&g_processed_counter_shva_lock);
    rc = g_processed_counter_shva;
    pthread_mutex_unlock(&g_processed_counter_shva_lock);
    return rc;
}

static void set_processed_counter_in(uint64_t cnt)
{
    pthread_mutex_lock(&g_processed_counter_in_lock);
    g_processed_counter_in = cnt;
    pthread_mutex_unlock(&g_processed_counter_in_lock);
}

static int64_t get_processed_counter_in(void)
{
    uint64_t rc;
    pthread_mutex_lock(&g_processed_counter_in_lock);
    rc = g_processed_counter_in;
    pthread_mutex_unlock(&g_processed_counter_in_lock);
    return rc;
}

static int get_shva_processing_status(void)
{
    int rc;
    pthread_mutex_lock(&shva_processing_st_lock);
    rc = shva_processing_st;
    pthread_mutex_unlock(&shva_processing_st_lock);
    return rc;
}

static void get_shva_processing_st_on(void)
{
    pthread_mutex_lock(&shva_processing_st_lock);
    shva_processing_st = 1;
    pthread_mutex_unlock(&shva_processing_st_lock);
}

static void get_shva_processing_st_off(void)
{
    pthread_mutex_lock(&shva_processing_st_lock);
    shva_processing_st = 0;
    pthread_mutex_unlock(&shva_processing_st_lock);
}

static void *processor_thread_shva(__attribute__((unused)) void *arg)
{
    int do_run = 1;
    pepa_core_t *core = arg;
    precessing_req_t *pr = NULL;

    TESTP_ASSERT(core, "core is NULL");

    char *buf = malloc(core->internal_buf_size);
    TESTP_ASSERT(buf, "can not allocate char *buf");

    do {
        do {
            pr = q_get(q_shva);
            if (NULL == pr) {
                usleep(100);
            }
        } while (NULL == pr);

        if (core->sockets.shva_rw == FD_CLOSED) {
            free(pr);
            continue;
        }

        get_shva_processing_st_on();

        pthread_mutex_lock(&g_shva_lock);
#if 0 /* SEB */  /* 15/11/2024 */
        int rc = pepa_one_direction_copy4(pr->core, buf, core->internal_buf_size,
                                          /* Send to : */pr->fd_out, /* Name of output socket */pr->name_out,
                                          /* From: */ pr->fd_in, /* Name of input socket */pr->name_in,
                                          /*Debug is ON */ 0,
                                          /* RX stat */pr->rx_stat,
                                          /* TX stat */pr->tx_stat,
                                          /* Max iterations */ /*5*/ 1);
#endif /* SEB */ /* 15/11/2024 */

        slog_note_l("BEFORE SHVA -> OUT");
        int rc = pepa_one_direction_copy4(pr->core,
                                          buf,
                                          core->internal_buf_size,
                                          /* Send to : */ core->sockets.out_write, /* Name of output socket */ "OUT",
                                          /* From: */ core->sockets.shva_rw, /* Name of input socket */ "SHVA",
                                          /*Debug is ON */ 0,
                                          /* RX stat */ pr->rx_stat,
                                          /* TX stat */ pr->tx_stat,
                                          /* Max iterations */ /*5*/ 1);

        slog_note_l("AFTER SHVA -> OUT");
        pthread_mutex_unlock(&g_shva_lock);
        if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
            slog_warn_l("SHVA is degraded, must restart sockets");
            set_thread_response(-PEPA_ERR_BAD_SOCKET_READ);
        }

        if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
            set_thread_response(-PEPA_ERR_BAD_SOCKET_WRITE);
        }

        get_shva_processing_st_off();

        if (PEPA_ERR_OK != rc) {
            slog_error_l("[PROC] [%ld] Can not transfer data from %s (fd = %d) to %s (fd = %d)",
                         pr->cnt, pr->name_in, pr->fd_in, pr->name_out, pr->fd_out);
        }

        if (PEPA_ERR_OK == rc) {
            pr->core->monitor.shva_reads++;
            pr->core->monitor.out_writes++;
        }

        free(pr);

    } while (do_run);

    return NULL;
}

static void *processor_thread_in(__attribute__((unused)) void *arg)
{
    int do_run = 1;
    const pepa_core_t *core = arg;
    precessing_req_t *pr = NULL;

    TESTP_ASSERT(core, "core is NULL");

    char *buf = malloc(core->internal_buf_size);
    TESTP_ASSERT(buf, "can not allocate char *buf");

    do {
        do {
            pr = q_get(q_in);
            if (NULL == pr) {
                usleep(10000);
            }
        } while (NULL == pr);

        /* Test the file descriptor is still in the IN */
        if (FD_IS_IN != pepa_if_fd_in(pr->core, pr->fd_in)) {
            free(pr);
            continue;
        }

        if (NO == pepa_util_is_socket_valid(pr->fd_in)) {
            free(pr);
            continue;
        }

        pthread_mutex_lock(&g_shva_lock);

        set_reader_processing_on(pr->core, pr->fd_in);
        int rc = pepa_one_direction_copy4(pr->core, buf, core->internal_buf_size,
                                          /* Send to : */ pr->fd_out, /* Name of output socket */ pr->name_out,
                                          /* From: */ pr->fd_in, /* Name of input socket */ pr->name_in,
                                          /*Debug is ON */ 0,
                                          /* RX stat */ pr->rx_stat,
                                          /* TX stat */ pr->tx_stat,
                                          /* Max iterations */ /*5*/ 1);

        pthread_mutex_unlock(&g_shva_lock);
        set_reader_processing_off(pr->core, pr->fd_in);

        if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
            // slog_warn_l("One of IN fds degraded, must be reset");
            //  pepa_in_reading_sockets_close_rm(pr->core, pr->fd_in);
        }

        if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
            slog_warn_l("SHVA is degraded, must restart sockets");
            set_thread_response(-PEPA_ERR_BAD_SOCKET_READ);
        }

#if 0 /* SEB */  /* 15/11/2024 */
        if (PEPA_ERR_OK != rc) {
            slog_error_l("[PROC] [%ld] Can not transfer data from %s (fd = %d) to %s (fd = %d)",
                         pr->cnt, pr->name_in, pr->fd_in, pr->name_out, pr->fd_out);
        }
#endif /* SEB */ /* 15/11/2024 */

        if (PEPA_ERR_OK == rc) {
            pr->core->monitor.in_reads++;
            pr->core->monitor.shva_writes++;
        }

        free(pr);

    } while (do_run);

    return NULL;
}

#define DUMP_BUF_LEN (1024)
static const char *dump_event(const int ev)
{
    static char str[DUMP_BUF_LEN];
    int offset = 0;

    if (ev & EPOLLIN) {
        offset = sprintf(str + offset, "%s", "EPOLLIN");
    }

    if (ev & EPOLLOUT) {
        offset = sprintf(str + offset, "%s | ", "EPOLLOUT");
    }

    if (ev & EPOLLRDHUP) {
        offset = sprintf(str + offset, "%s | ", "EPOLLRDHUP");
    }

    if (ev & EPOLLPRI) {
        offset = sprintf(str + offset, "%s | ", "EPOLLPRI");
    }

    if (ev & EPOLLERR) {
        offset = sprintf(str + offset, "%s | ", "EPOLLERR");
    }

    if (ev & EPOLLHUP) {
        offset = sprintf(str + offset, "%s | ", "EPOLLHUP");
    }

    if (ev & EPOLLET) {
        offset = sprintf(str + offset, "%s | ", "EPOLLET");
    }

    if (ev & EPOLLONESHOT) {
        offset = sprintf(str + offset, "%s | ", "EPOLLONESHOT");
    }
    if (ev & EPOLLWAKEUP) {
        offset = sprintf(str + offset, "%s | ", "EPOLLWAKEUP");
    }
    if (ev & EPOLLEXCLUSIVE) {
        sprintf(str + offset, "%s | ", "EPOLLEXCLUSIVE");
    }

    return str;
}

static int process_sockets(pepa_core_t *core,
                           const int fd_out,
                           char *name_out,
                           const int fd_in,
                           char *name_in,
                           uint64_t *rx_stat,
                           uint64_t *tx_stat,
                           const int ev)
{
    // pthread_t thread_id;

    if (fd_in == core->sockets.shva_rw) {
        if ((1 == get_shva_processing_status())) {
            return PEPA_ERR_OK;
        }
    } else {
        if (PROCESSONG_ON == test_reader_processing_on(core, fd_in)) {
            return PEPA_ERR_OK;
        }
    }

    int rc = bytes_available_read(fd_in);
    if (rc < 1) {
        slog_note_l("There's no bytes available on socket %s (fd = %d), returned: %d", name_in, fd_in, rc);
        slog_note_l("IN event: %s", dump_event(ev));
        return PEPA_ERR_OK;
    }

    precessing_req_t *pr = calloc(sizeof(precessing_req_t), 1);
    TESTP(pr, PEPA_ERR_BUF_ALLOCATION);
    pr->fd_in = fd_in;
    pr->fd_out = fd_out;
    pr->name_in = name_in;
    pr->name_out = name_out;
    pr->rx_stat = rx_stat;
    pr->tx_stat = tx_stat;
    // pr->cnt = g_counter++;
    pr->core = core;

    if (fd_in == core->sockets.shva_rw) {
        pr->cnt = get_processed_counter_shva();
        set_processed_counter_shva(pr->cnt + 1);
    } else {
        pr->cnt = get_processed_counter_in();
        set_processed_counter_in(pr->cnt + 1);
    }

    // int rc = pthread_create(&thread_id, NULL, processor_thread, pr);
    // if (0 != rc) {
    //     slog_error_l("Can not create processor thread: %s", strerror(errno));
    //     return -1;
    //  }

    if (fd_in == core->sockets.shva_rw) {
        q_put(q_shva, pr);
    } else {
        q_put(q_in, pr);
    }

    return PEPA_ERR_OK;
}

static int pepa_process_fdx_shva_rw_one(pepa_core_t *core, const int ev)
{
    int32_t rc = pepa_process_fdx_analyze_error(ev, "SHVA");

    if (PEPA_ERR_OK != rc) {
        slog_error_l("[%s] Error on socket", "SHVA");
        return rc;
    }

    /* Do we have a buffer waiting? If no, it is strabge */
    if (!(ev & EPOLLIN)) {
        slog_error_l("[SHVA] I would expect here an incoming buffer, but there is not. Some error?");
        return PEPA_ERR_OK;
    }
    if (ev & EPOLLIN) {
        rc = process_sockets(core,
                             /* Send to : */ core->sockets.out_write, "OUT",
                             /* From: */ core->sockets.shva_rw, "SHVA",
                             /* RX stat */ &core->monitor.shva_rx,
                             /* TX stat */ &core->monitor.out_tx,
                             ev);
    }
    // usleep(10000);

#if 0 /* SEB */  /* 14/11/2024 */
    rc = pepa_one_direction_copy4(core,
                                  /* Send to : */core->sockets.out_write, "OUT",
                                  /* From: */ core->sockets.shva_rw, "SHVA",
                                  /*Debug is ON */ 0,
                                  /* RX stat */&core->monitor.shva_rx,
                                  /* TX stat */&core->monitor.out_tx,
                                  /* Max iterations */ /*5*/ 1);

    if (PEPA_ERR_OK == rc) {
        //slog_warn_l("%s: Sent from socket %d", "IN-FORWARD", events[i].data.fd);
        core->monitor.out_writes++;
        core->monitor.shva_reads++;

        return PEPA_ERR_OK;
    }


    slog_note_l("[SHVA] An error on sending buffers from SHVA to OUT: %s", pepa_error_code_to_str(rc));

    /* Something wrong with the socket, should be removed */

    /* Writing side is off, means: SHVA or OUT socket is invalid */
    /* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
    if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
        slog_note_l("[SHVA] Could not write to %s; setting system to FAIL", "OUT");
    }

    if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
        /* Here are two cases: the read can be IN or SHVA. IN case of SHVA we must restart all sockets */
        slog_note_l("[SHVA] Could not read from %s; setting system to FAIL", "SHVA");
    }
#endif /* SEB */ /* 14/11/2024 */

    return rc;
}

/* Process one event related to IN sockets */
static int pepa_process_fdx_in_one(pepa_core_t *core, const int ev, const int fd)
{
    int32_t rc = pepa_process_fdx_analyze_error(ev, "IN");

    /* Process exception(s) */
    if (PEPA_ERR_OK != rc) {
        slog_note_l("[IN] Error on IN socket: %s", pepa_error_code_to_str(rc));
        pepa_remove_socket_from_epoll(core, fd, "IN", __FILE__, __LINE__);
        pepa_in_reading_sockets_close_rm(core, fd);
        return PEPA_ERR_OK;
    }

    /* Do we have a buffer waiting? If no, it is strabge */

    if (ev & EPOLLIN) {
        rc = process_sockets(core,
                             /* Send to : */ core->sockets.shva_rw, "SHVA",
                             /* From: */ fd, "IN",
                             /* RX stat */ &core->monitor.in_rx,
                             /* TX stat */ &core->monitor.shva_tx, ev);
        // usleep(10000);
    }

#if 0 /* SEB */  /* 14/11/2024 */
    /* Here; there is EPOLLIN event */
    rc = pepa_one_direction_copy4(core,
                                  /* Send to : */core->sockets.shva_rw, "SHVA",
                                  /* From: */ fd, "IN",
                                  /*Debug is ON */ 0,
                                  /* RX stat */&core->monitor.in_rx,
                                  /* TX stat */&core->monitor.shva_tx,
                                  /* Max iterations */ /*5*/ 1);

    if (PEPA_ERR_OK == rc) {
        core->monitor.shva_writes++;
        core->monitor.in_reads++;

        return PEPA_ERR_OK;
    }


    const char *err_str = strerror(errno);

    slog_note_l("[IN] An error on sending buffers from IN to SHVA: %s", pepa_error_code_to_str(rc));

    /* Something wrong with the socket, should be removed */

    /* Writing side is off, means: SHVA or OUT socket is invalid */
    /* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
    if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
        slog_note_l("[IN] Could not write to %s; setting system to FAIL", "OUT");
        pepa_in_reading_sockets_close_rm(core, fd);
    }

    if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
        /* Here are two cases: the read can be IN or SHVA. IN case of SHVA we must restart all sockets */
        slog_note_l("[IN] Could not read from %s; setting system to FAIL", "SHVA");
        pepa_in_reading_sockets_close_rm(core, fd);
    }

    slog_note_l("[IN] Could not read from IN; Unknown error: %d (%s)", rc, err_str);
#endif /* SEB */ /* 14/11/2024 */

    return rc;
}

/**
 * @author Sebastian Mountaniol (1/7/24)
 * @brief Process waiting signals on all sockets
 * @param pepa_core_t* core       Core structure
 * @param struct epoll_event[] events     Events from epoll
 * @param int event_count Number of events
 * @return int Returns PEPA_ERR_OK if all processed and no
 *  	   errors.Else returns a status of state machine which
 *  	   requres to reset all sockets.
 * @details
 */
static int pepa_process_fdx_new(pepa_core_t *core, const struct epoll_event events_array[], const int event_count)
{
    // int32_t rc = PEPA_ERR_OK;
    int32_t i;

    for (i = 0; i < event_count; i++) {
        const int fd = events_array[i].data.fd;
        const uint32_t ev = events_array[i].events;

        /* An event on SHVA reading socket */
        if (fd == core->sockets.shva_rw) {
            const int rc = pepa_process_fdx_shva_rw_one(core, ev);
            if (PEPA_ERR_OK != rc) {
                slog_error_l("Error on SHVA RW socket");
                return TE_RESTART;
            }
        }

        /* An event on IN LISTEN : a new connection or disconnection */
        if (fd == core->sockets.in_listen) {
            const int rc = pepa_in_accept_new_connection(core);

            /* If somethins happened during this process, we stop and return */
            if (PEPA_ERR_OK != rc) {
                slog_error_l("Error on IN LISTEN socket");
                return rc;
            }

            return PEPA_ERR_OK;
        }

        /* If we got an event on OUT LISTEN socket, it is probably an error, we do not eexpect any activity there */
        if (fd == core->sockets.out_listen) {
            const int rc = pepa_process_fdx_analyze_error(ev, "OUT LISTEN");
            if (PEPA_ERR_OK != rc) {
                slog_error_l("Error on OUT LISTEN socket, need to reset");
                return TE_RESTART;
            }
        }

        /* An event on IN listening socket: a buffer waiting or disconnect */
        if (FD_IS_IN == pepa_if_fd_in(core, fd)) {
            const int rc = pepa_process_fdx_in_one(core, ev, fd);
            if (PEPA_ERR_OK != rc) {
                slog_error_l("Error on IN READER socket: %s", pepa_error_code_to_str(rc));
                return TE_RESTART;
            }
        }
    }

    // slog_note_l ("No errors");
    return PEPA_ERR_OK;
}

__attribute__((unused))
#if 1 /* SEB */ /* 13/11/2024 */
static int
pepa_process_fdx(pepa_core_t *core, const struct epoll_event events_array[], const int event_count)
{
    int32_t rc = PEPA_ERR_OK;
    int32_t i;

    rc = pepa_process_fdx_shva(core, events_array, event_count);
    if (PEPA_ERR_OK != rc) {
        return rc;
    }

    for (i = 0; i < event_count; i++) {
        const int fd = events_array[i].data.fd;
        const uint32_t events = events_array[i].events;

        if (!(events & EPOLLIN)) {
            continue;
        }

        if (core->sockets.shva_rw == fd) {
            continue;
        }

        /* The IN socket: listening, if there is an event, we should to open a new connection */
        if (core->sockets.in_listen == fd) {
            rc = pepa_in_accept_new_connection(core);

            /* If somethins happened during this process, we stop and return */
            if (PEPA_ERR_OK != rc) {
                return rc;
            }
            continue;
        }

        /* Below this point we expect onli IN READ sockets */
        if (pepa_if_fd_in(core, fd)) {
            continue;
        }

        /* Read /write from/to socket */

        // transfer_data4
        // rc = pepa_one_direction_copy3(core,
        rc = pepa_one_direction_copy4(/* 1 */ core,
                                      /* 2 */ core->buffer,
                                      /* 3 */ core->internal_buf_size,
                                      /* 4 */ /* Send to : */ core->sockets.shva_rw,
                                      /* 5 */ "SHVA",
                                      /* 6 */ /* From: */ fd,
                                      /* 7 */ "IN",
                                      /* 8 */ /*Debug is ON */ 0,
                                      /* 9 */ /* RX stat */ (uint64_t *)&core->monitor.in_rx,
                                      /* 10 */ /* TX stat */ (uint64_t *)&core->monitor.shva_tx,
                                      /* 11 */ /* Max iterations */ 1);

        if (PEPA_ERR_OK == rc) {
            continue;
        }

        slog_note_l("An error on sending buffers: %s", pepa_error_code_to_str(rc));

        /* Something wrong with the socket, should be removed */

        /* Writing side is off, means: SHVA or OUT socket is invalid */
        /* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
        if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
            slog_note_l("Could not write to %s; setting system to FAIL", "SHVA");
            return TE_RESTART;
        }

        if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
            /* If it is not SHVA, it is one of IN sockets; just close and remove from the set */

            slog_note_l("Could not read from %s; removing the [IN] [%d] socket", "IN", fd);

            /* Remove the broken IN read socket from the epoll */
            pepa_remove_socket_from_epoll(core, fd, "IN", __FILE__, __LINE__);

            /* Close the broken IN read socket */
            pepa_in_reading_sockets_close_rm(core, fd);
        }
    }
    return PEPA_ERR_OK;
}
#endif /* SEB */ /* 13/11/2024 */

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief This is the main loop where buffers are transferred from
 *        socket to socket
 * @param pepa_core_t* core  Core structure
 * @return int The next state machine status
 * @details This loop is an infinite loop which works as long as
 *          all connections are valid. It executes epool_wait,
 *          and when an event occurs, it tests all file
 *          descriptors, find errors, transfer packets.
 *          It dynamically opens new IN readers and removes them
 *          when another side is disconnected. It returns with
 *          the next state machine status when something is
 *          Brocken: SHVA socket rw disconnected, or the OUT writing
 *          socket disconnected.
 */

static int pepa3_transfer_loop_new(pepa_core_t *core)
{
    int next_state = PST_TRANSFER_LOOP;
    int rc;
    struct epoll_event events[EVENTS_NUM];
    struct epoll_event events_copy[EVENTS_NUM];

    slog_note_l("/// BEGIN: TRANSFER PHASE ///");

    /* Wait until something happened */
    do {
        int32_t event_count = epoll_wait(core->epoll_fd, events, EVENTS_NUM, EPOLL_TIMEOUT);

        if (0 != get_thread_response()) {
            slog_error_l("One of processing thread fault; stop");
            return PST_CLOSE_SOCKETS;
        }

        /* Interrupted by a signal */
        if (event_count < 0 && EINTR == errno) {
            continue;
        }

        core->monitor.events += event_count;

        /* Copy the evens array, this way they are not changed when we iterate them several times:
           When we iterate events AND read / write the file descriptors, the event array might be reseted, and it is not what we need */
        memcpy(events_copy, events, sizeof(struct epoll_event) * EVENTS_NUM);

        /* Process buffers */

        core->monitor.in_fdx = 1;
        rc = pepa_process_fdx(core, events_copy, event_count);
        core->monitor.in_fdx = 0;

        switch (rc) {
            case PEPA_ERR_OK:
                break;
            case TE_RESTART:
                /* An error on SHVA or OUT listening sockets occured, all sockets should be restarted */
                // next_state = PST_CLOSE_SOCKETS;
                // break;
                slog_error_l("Need close sockets");
                return PST_CLOSE_SOCKETS;
            case TE_IN_RESTART:
                /* An error on listening IN socket occured, the IN socket should be restarted */
                // next_state = PST_RESTART_IN;
                // break;
                slog_error_l("Need restart sockets");
                return PST_RESTART_IN;
            case TE_IN_REMOVED:
                /* Do nothing, this is a normal situation, one of IN readers was removed */
                /* However do not process file descriptors on this iteration;
                   start a new iteration and pn the new iteration the removed socket will not appear in the set */

                /* However, if there 0 IN sockets left, we should restart all sockets */
                slog_error_l("Read socket removed sockets");
                if (core->in_reading_sockets.active < 1) {
                    slog_note_l("No IN reading sockets left (allclosed), we should restart IN and SHVA");
                    return PST_RESTART_IN;
                }
                continue;
                // break;
            default:
                slog_error_l("You should never be here: got status %d", rc);
                slog_error_l("/// ERROR: TRANSFER PHASE ///");
                abort();
        }

        /* If there is an error, we must restart all sockets */
        if (TE_RESTART == rc) {
            slog_note_l("/// FINISH: TRANSFER PHASE ///");
            return PST_CLOSE_SOCKETS;
        }

        /* If there is an error, we must restart all sockets */
        if (PEPA_ERR_OK != rc) {
            slog_note_l("/// FINISH: TRANSFER PHASE ///");
            return PST_CLOSE_SOCKETS;
        }
    } while (1);

    slog_note_l("/// FINISH: TRANSFER PHASE ///");
    return next_state;
}

static int pepa3_transfer_loop(pepa_core_t *core)
{
    int next_state = PST_TRANSFER_LOOP;
    int rc;
    struct epoll_event events[EVENTS_NUM];
    struct epoll_event events_copy[EVENTS_NUM];

    slog_note_l("/// BEGIN: TRANSFER PHASE ///");

    /* Wait until something happened */
    do {
        int32_t event_count = epoll_wait(core->epoll_fd, events, EVENTS_NUM, EPOLL_TIMEOUT);

        /* Interrupted by a signal */
        if (event_count < 0 && EINTR == errno) {
            continue;
        }

        core->monitor.events += event_count;

        /* Copy the evens array, this way they are not changed when we iterate them several times:
           When we iterate events AND read / write the file descriptors, the event array might be reseted, and it is not what we need */
        memcpy(events_copy, events, sizeof(struct epoll_event) * EVENTS_NUM);

        /* Process exceptions */
        rc = pepa_process_exceptions(core, events_copy, event_count);

        switch (rc) {
            case PEPA_ERR_OK:
                break;
            case TE_RESTART:
                /* An error on SHVA or OUT listening sockets occured, all sockets should be restarted */
                // next_state = PST_CLOSE_SOCKETS;
                // break;
                slog_error_l("Need close sockets");
                return PST_CLOSE_SOCKETS;
            case TE_IN_RESTART:
                /* An error on listening IN socket occured, the IN socket should be restarted */
                // next_state = PST_RESTART_IN;
                // break;
                slog_error_l("Need restart sockets");
                return PST_RESTART_IN;
            case TE_IN_REMOVED:
                /* Do nothing, this is a normal situation, one of IN readers was removed */
                /* However do not process file descriptors on this iteration;
                   start a new iteration and pn the new iteration the removed socket will not appear in the set */

                /* However, if there 0 IN sockets left, we should restart all sockets */
                slog_error_l("Read socket removed sockets");
                if (core->in_reading_sockets.active < 1) {
                    slog_note_l("No IN reading sockets left (allclosed), we should restart IN and SHVA");
                    return PST_RESTART_IN;
                }
                continue;
                // break;
            default:
                slog_error_l("You should never be here: got status %d", rc);
                slog_error_l("/// ERROR: TRANSFER PHASE ///");
                abort();
        }

        /* If there is an error, we must restart all sockets */
        if (TE_RESTART == rc) {
            slog_note_l("/// FINISH: TRANSFER PHASE ///");
            return PST_CLOSE_SOCKETS;
        }

        /* Process buffers */
        rc = pepa_process_fdx(core, events_copy, event_count);

        /* If there is an error, we must restart all sockets */
        if (PEPA_ERR_OK != rc) {
            slog_note_l("/// FINISH: TRANSFER PHASE ///");
            return PST_CLOSE_SOCKETS;
        }
    } while (1);

    slog_note_l("/// FINISH: TRANSFER PHASE ///");
    return next_state;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Create OUT listening socket
 * @param pepa_core_t* core  Core structure
 * @details This is a blocking function. When it finished,
 *  		the OUT listening socket is created.

 */
static void pepa_out_open_listening_socket(pepa_core_t *core)
{
    struct sockaddr_in s_addr;

    if (core->sockets.out_listen >= 0) {
        slog_note_l("Trying to open a listening socket while it is already opened");
    }
    do {
        /* Just try to close it */
        core->sockets.out_listen = pepa_open_listening_socket(&s_addr,
                                                              core->out_thread.ip_string,
                                                              core->out_thread.port_int,
                                                              (int)core->out_thread.clients,
                                                              __func__);
        if (core->sockets.out_listen < 0) {
            core->sockets.out_listen = FD_CLOSED;
            // slog_warn_l("Can not open listening socket: %s", strerror(errno));
            usleep(1000000);
        }
    } while (core->sockets.out_listen < 0);
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief This function implements OUT writing socket accept.
 *  	  THis function will be vlocked until an external actor
 *  	  is connected
 * @param const pepa_core_t* core     Core structure
 * @param const int32_t fd_listen Listening OUT socket
 * @return int32_t Socket file descriptor of the writing OUT
 *  	   socket
 */
static int32_t pepa_out_wait_connection(__attribute__((unused))
                                        const pepa_core_t *core,
                                        const int32_t fd_listen)
{
    struct sockaddr_in s_addr;
    socklen_t addrlen = sizeof(struct sockaddr);
    int32_t fd_read = FD_CLOSED;
    do {
        slog_info_l("Starting accept() waiting");
        fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC);
    } while (fd_read < 0);

    slog_info_l("ACCEPTED CONNECTION: fd = %d", fd_read);
    pepa_set_tcp_timeout(fd_read);
    // pepa_set_tcp_send_size(core, fd_read, "OUT");
    slog_note_l("Socket now it is %s", utils_socked_blocking_or_not(fd_read));
    return fd_read;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Wait on OUT listening socket incoming connection
 * @param pepa_core_t* core  Core structure
 * @details This is a blocking function. When it finished,
 *  		the OUT writing socket is valid adn ready.
 */
static void pepa_thread_accept(pepa_core_t *core)
{
    core->sockets.out_write = pepa_out_wait_connection(core, core->sockets.out_listen);
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Open connection to SHVA server
 * @param pepa_core_t* core  Core structure
 * @details This is a blocking function. When it finished,
 *  		the SHVA socket is opened and valid.
 */
static void pepa_shva_open_connection(pepa_core_t *core)
{
    /* Open connnection to the SHVA server */
    do {
        core->sockets.shva_rw = pepa_open_connection_to_server(core->shva_thread.ip_string->data, core->shva_thread.port_int, __func__);

        if ((core->sockets.shva_rw < 0) &&
            (FD_CLOSED != core->sockets.shva_rw)) {
            core->sockets.shva_rw = FD_CLOSED;
            usleep(100000);
            continue;
        }
    } while (core->sockets.shva_rw < 0);

    slog_note_l("Opened connection to SHVA");
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Create IN listening socket
 * @param pepa_core_t* core  Core strucutre
 * @details This is a blocking function. When it finished,
 *  		the IN listening socket is opened.
 */
static void pepa_in_open_listen_socket(pepa_core_t *core)
{
    struct sockaddr_in s_addr;
    while (1) {
        /* Just try to close it */
        if (core->sockets.in_listen >= 0) {
            pepa_socket_close_in_listen(core);
        }

        core->sockets.in_listen = pepa_open_listening_socket(&s_addr,
                                                             core->in_thread.ip_string,
                                                             core->in_thread.port_int,
                                                             (int)core->in_thread.clients,
                                                             __func__);
        if (core->sockets.in_listen >= 0) {
            return;
        }
        usleep(1000);
    }
}

/*********************************/
/*** State Machine functions ***/
/*********************************/

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief The first step of the state machine. Allocate all
 *  	  needded structures, create epoll file descriptor
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 */
static int pepa3_start(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: START PHASE ///");
    core->buffer = calloc(core->internal_buf_size, 1);

    if (NULL == core->buffer) {
        slog_error_l("Can not allocate a transfering buffer, stopped");
        slog_error_l("/// ERROR: START PHASE ///");
        exit(1);
    }
    core->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (core->epoll_fd < 0) {
        slog_error_l("Can not create eventfd file descriptor, stopped");
        free(core->buffer);
        slog_error_l("/// ERROR: START PHASE ///");
        exit(2);
    }

    /* TODO: Instead of 1024 make it configurable */
    pepa_in_reading_sockets_allocate(core, PEPA_IN_SOCKETS);

    slog_note_l("Finished 'start' phase");
    slog_note_l("/// FINISH: START PHASE ///");
    return PST_WAIT_OUT;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Close all sockets and remove them from the epoll set
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 * @details
 */
int pepa3_close_sockets(pepa_core_t *core)
{
    int rc;

    /* Remove sockets from the the epoll set */

    pepa_remove_socket_from_epoll(core, core->sockets.in_listen, "IN LISTEN", __FILE__, __LINE__);
    pepa_remove_socket_from_epoll(core, core->sockets.out_listen, "OUT LISTEN", __FILE__, __LINE__);
    pepa_remove_socket_from_epoll(core, core->sockets.out_write, "OUT WRITE", __FILE__, __LINE__);
    pepa_remove_socket_from_epoll(core, core->sockets.shva_rw, "SHVA RW", __FILE__, __LINE__);

    pepa_in_reading_sockets_close_all(core);
    rc = pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN");
    if (rc) {
        slog_warn_l("Could not close socket SHVA: fd: %d", core->sockets.shva_rw);
    }
    core->sockets.in_listen = FD_CLOSED;

    pepa_reading_socket_close(core->sockets.shva_rw, "SHVA");
    core->sockets.shva_rw = FD_CLOSED;

    pepa_socket_close(core->sockets.out_write, "OUT WRITE");
    core->sockets.out_write = FD_CLOSED;

    rc = pepa_socket_shutdown_and_close(core->sockets.out_listen, "OUT LISTEN");
    if (rc) {
        slog_warn_l("Could not close socket OUT LISTEN: fd: %d", core->sockets.out_listen);
    }

    core->sockets.out_listen = FD_CLOSED;

    slog_note_l("Finished 'close sockets' phase");
    return PST_WAIT_OUT;
}

static void pape3_close_shva_rw_all(pepa_core_t *core)
{
    TESTP_ASSERT(core, "core is NULL");
    pepa_remove_socket_from_epoll(core, core->sockets.shva_rw, "SHVA RW", __FILE__, __LINE__);
    pepa_reading_socket_close(core->sockets.shva_rw, "SHVA");
    core->sockets.shva_rw = FD_CLOSED;
}

static void pape3_close_out_rw_all(pepa_core_t *core)
{
    TESTP_ASSERT(core, "core is NULL");
    slog_note_l("[OUT] Closing all OUT sockets");
    pepa_reading_socket_close(core->sockets.out_write, "OUT WRITE");
    pepa_socket_close(core->sockets.out_write, "OUT WRITE");
    core->sockets.out_write = FD_CLOSED;
    slog_note_l("[OUT] Closed all OUT sockets");
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Reset sockets: remove from epoll set and close all
 *  	  reading/writing sockets, but keep all listening
 *  	  sockets alive.
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 * @details
 */
static int pepa3_reset_rw_sockets(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: SOCKET RESET PHASE ///");

    pape3_close_shva_rw_all(core);
    pape3_close_out_rw_all(core);

    /* Close all IN reading sockets */
    pepa_in_reading_sockets_close_all(core);

    slog_note_l("/// FINISH: SOCKET RESET PHASE ///");
    return PST_WAIT_OUT;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Open OUT listening socket, adds it to epoll set,
 *  	  then waits for the incomming connection and adds it to
 *  	  epoll set
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 */
static int pepa3_wait_out(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: WAIT OUT PHASE ///");
    /* Both these functions are blocking and when they returned, both OUT sockets are opened */
    if (core->sockets.out_listen < 0) {
        pepa_out_open_listening_socket(core);
        if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.out_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            slog_error_l("Can not add OUT Listen socket to epoll set: %s", strerror(errno));
            slog_error_l("/// ERROR: WAIT OUT PHASE ///");
            return PST_CLOSE_SOCKETS;
        }
    }

    pepa_thread_accept(core);

    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add OUT Write socket to epoll set: %s", strerror(errno));
        slog_error_l("/// ERROR: WAIT OUT PHASE ///");
        return PST_CLOSE_SOCKETS;
    }

    slog_note_l("opened OUT socket: LISTEN: fd = %d, WRITE: fd = %d", core->sockets.out_listen, core->sockets.out_write);
    slog_note_l("/// FINISH: WAIT OUT PHASE ///");
    // return PST_OPEN_SHVA;
    /* New State Machine */
    return PST_START_IN;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief This function implements IN writing socket accept. This function will be locked until an external
 *        actor is connected
 * @param const pepa_core_t* core     Core structure
 * @param const int32_t fd_listen Listening OUT socket
 * @return int32_t Socket file descriptor of the writing OUT socket
 */
static int32_t pepa_wait_connection(const pepa_core_t *core, const int32_t fd_listen, const char *name)
{
    struct sockaddr_in s_addr;
    socklen_t addrlen = sizeof(struct sockaddr);
    int32_t fd_read = FD_CLOSED;

    memset(&s_addr, 0, sizeof(struct sockaddr_in));

    do {
        slog_info_l("Starting accept() waiting");
        fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC);
    } while (fd_read < 0);

    slog_info_l("ACCEPTED [%s] CONNECTION: fd = %d", name, fd_read);
    pepa_set_tcp_timeout(fd_read);
    pepa_set_tcp_send_size(core, fd_read, name);
    return fd_read;
}

static void pepa_accept_in_connections(pepa_core_t *core)
{
    slog_info_l(">>>>> Starting adding %ld IN Read sockets to epoll set", core->readers_preopen);

    do {
        int32_t fd_read = pepa_wait_connection(core, core->sockets.in_listen, "IN");
        pepa_in_reading_sockets_add(core, fd_read);

        if (0 != epoll_ctl_add(core->epoll_fd, fd_read, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            slog_error_l("Can not add IN Read socket (%d) to epoll set: %s", fd_read, strerror(errno));
            pepa_in_reading_sockets_close_rm(core, fd_read);
        }
        slog_info_l("Added IN Read socket (fd = %d) to epoll set", fd_read);
    } while (core->in_reading_sockets.active < (int)core->readers_preopen);

    slog_note_l(">>>>> Finished adding %ld IN Read sockets to epoll set", core->readers_preopen);
}

/**
 * @author se (9/16/24)
 * @brief Wait until several instances of the IN sockets are
 *        connected
 * @param pepa_core_t *core   Core structure
 * @return int Return the next state of the state machine
 */
// NEW
static int pepa3_wait_in(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: WAIT IN PHASE ///");

    /* Both these functions are blocking and when they returned, both OUT sockets are opened */
    if (core->sockets.in_listen < 0) {
        slog_error_l("IN Listening socket is closed, it must be opened");
        slog_error_l("/// ERROR: WAIT IN PHASE ///");
        return PST_START_IN;
    }

    pepa_accept_in_connections(core);

    /* Add all accepted IN connection to the epoll */
    slog_note_l("/// FINISH: WAIT IN PHASE ///");
    return PST_OPEN_SHVA;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Open connection to SHVA server
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 * @details
 */
static int pepa3_open_shva(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: OPEN SHVA PHASE ///");
    /* This is an blocking function, returns only when SHVA is opened */
    pepa_shva_open_connection(core);
    slog_note_l("Opened shva socket: Blocking? %s", utils_socked_blocking_or_not(core->sockets.shva_rw));
    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.shva_rw, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add SHVA socket to epoll set: %s", strerror(errno));
        slog_error_l("/// ERROR: OPEN SHVA PHASE ///");
        return PST_CLOSE_SOCKETS;
    }
    slog_note_l("Added shva socket to epoll: Blocking? %s", utils_socked_blocking_or_not(core->sockets.shva_rw));

    slog_note_l("Opened SHVA socket: fd = %d", core->sockets.shva_rw);
    slog_note_l("/// FINISH: OPEN SHVA PHASE ///");
    // return PST_START_IN;
    /* New State Machine */
    return PST_TRANSFER_LOOP;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Start IN listening socket and add it to the epoll set
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 * @details
 */
static int pepa3_start_in(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: START IN PHASE ///");
    /* This is a blocking function */
    if (core->sockets.in_listen >= 0) {
        slog_note_l("/// FINISH ON START: START IN PHASE (in_listen > 0) ///");
        return PST_WAIT_IN;
    }

    pepa_in_open_listen_socket(core);

    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.in_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add IN Listen socket to epoll set: %s", strerror(errno));
        slog_error_l("/// ERROR: START IN PHASE ///");
        return PST_CLOSE_SOCKETS;
    }
    slog_note_l("Opened IN LISTEN: fd = %d", core->sockets.in_listen);
    slog_note_l("/// FINISH: START IN PHASE ///");
    // return PST_TRANSFER_LOOP;
    /* New State Machine */
    return PST_WAIT_IN;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Remove all IN related sockets and start IN over
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 * @details
 */
static int pepa3_restart_in(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: RESTART IN PHASE ///");
    pape3_close_shva_rw_all(core);

    /* Remove IN listening socket from the epoll */
    pepa_reading_socket_close(core->sockets.in_listen, "IN LISTEN");

    /* Close and remove from epoll all reading sockets */
    pepa_in_reading_sockets_close_all(core);

    // rc = pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN");
    pape3_close_shva_rw_all(core);

    core->sockets.in_listen = FD_CLOSED;
    // return pepa3_start_in(core);
    /* New State Machine */
    slog_note_l("/// FINISH: RESTART IN PHASE ///");
    return PST_START_IN;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Finish everything before the state machine loop exits
 * @param pepa_core_t* core  Core structure
 * @return int The next state machine state
 * @details
 */
static int pepa3_end(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: END PHASE ///");
    pepa3_close_sockets(core);
    pepa_in_reading_sockets_free(core);
    close(core->epoll_fd);
    slog_note_l("/// FINISH: END PHASE ///");
    return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief This function is entry point and implementation of the
 *  	  state machine
 * @param pepa_core_t* core  Core structure
 * @return int PEPA_ERR_OK when state machine is termonated.
 *  	   However, this function should not terminate ever.
 * @details
 */
int pepa_go(pepa_core_t *core)
{
    TESTP(core, -1);
    if (NO == pepa_core_is_valid(core)) {
        slog_error_l("Core structure is invalid");
        return -1;
    }
    int next_state = PST_START;
    pthread_t thread_shva_id;
    pthread_t thread_in_id;

    init_queues();
#if 0 // Sebastian Mountaniol / 2025-04-13 16:28:20
    if (rc) {
        slog_fatal_l("Can not create queues");
        abort();
    }
#endif // 0 / Sebastian Mountaniol / 2025-04-13 16:28:20

    rc = pthread_create(&thread_in_id, NULL, processor_thread_in, core);
    if (rc) {
        slog_fatal_l("Can not create processor_thread_in");
        abort();
    }

    rc = pthread_create(&thread_shva_id, NULL, processor_thread_shva, core);
    if (rc) {
        slog_fatal_l("Can not create processor_thread_shva");
        abort();
    }

    usleep(10000);

    do {
        switch (next_state) {
            case PST_START:
                next_state = pepa3_start(core);
                // Next PST_WAIT_OUT
                break;

            case PST_CLOSE_SOCKETS:
                // next_state = pepa3_close_sockets(core);
                next_state = pepa3_reset_rw_sockets(core);
                // ORIG: Next PST_WAIT_OUT
                break;

            case PST_WAIT_OUT:
                next_state = pepa3_wait_out(core);
                // ORIG: Next PST_OPEN_SHVA
                // NEW: Next PST_START_IN // DONE
                break;

            case PST_START_IN:
                next_state = pepa3_start_in(core);
                // ORIG: Next PST_TRANSFER_LOOP
                // NEW: Next PST_WAIT_IN // DONE
                break;

            case PST_WAIT_IN:
                next_state = pepa3_wait_in(core);
                // NEW: Next PST_OPEN_SHVA // TODO
                break;

            case PST_RESTART_IN:
                next_state = pepa3_restart_in(core);
                // ORIG: Next: PST_TRANSFER_LOOP
                // NEW: Should first close SHVA // TODO
                // NEW: Next: PST_START_IN // TODO
                break;

            case PST_OPEN_SHVA:
                next_state = pepa3_open_shva(core);
                // ORIG: Next PST_START_IN
                // NEW: Next PST_TRANSFER_LOOP // DONE
                break;

            case PST_TRANSFER_LOOP:
                next_state = pepa3_transfer_loop(core);
                // ORIG: Next: PST_RESTART_IN or PST_CLOSE_SOCKETS
                break;

            case PST_END:
                pthread_cancel(thread_shva_id);
                pthread_cancel(thread_in_id);
                usleep(100000);

                return pepa3_end(core);
            default:
                slog_error_l("You should never be here");
                abort();
        }
    } while (1);

    return PEPA_ERR_OK;
}
