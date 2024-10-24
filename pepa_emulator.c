#define _GNU_SOURCE
#include <pthread.h>
#include <sys/param.h>
#include <unistd.h> /* For read() */
#include <sys/epoll.h>
#include <errno.h>

#include "pepa_emulator.h"
#include "buf_t/buf_t.h"
#include "pepa_config.h"
#include "pepa_core.h"
#include "slog/src/slog.h"
#include "pepa_errors.h"
#include "pepa_parser.h"
#include "pepa_server.h"
#include "pepa_socket_common.h"
#include "pepa_state_machine.h"

/* Sleep time between sending a buffer */

int        should_exit          = 0;
pthread_t  control_thread_id;
emu_t      *emu                 = NULL;

                     #define SHUTDOWN_DIVIDER (100003573)
                     #define SHVA_SHUTDOWN_DIVIDER (10000357)
                     //#define SHVA_SHUTDOWN_DIVIDER (1000035)
                     #define SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHUTDOWN_DIVIDER))
                     #define SHVA_SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHVA_SHUTDOWN_DIVIDER))

                     #define RX_TX_PRINT_DIVIDER (1000000)

                     #define PEPA_MIN(a,b) ((a<b) ? a : b )

/* Keep here PIDs of IN threads */
pthread_t  *in_thread_idx;


const char *lorem_ipsum         = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.\0";
uint64_t   lorem_ipsum_len      = 0;

buf_t      *lorem_ipsum_buf     = NULL;

// void *pepa_emulator_in_thread(__attribute__((unused))void *arg);
// void pepa_emulator_in_thread_cleanup(__attribute__((unused))void *arg);
// int32_t         in_start_connection(void);
// void *pepa_emulator_shva_thread(__attribute__((unused))void *arg);
// void pepa_emulator_shva_thread_cleanup(__attribute__((unused))void *arg);
// void *pepa_emulator_shva_writer_thread(__attribute__((unused))void *arg);
// void *pepa_emulator_shva_reader_thread(__attribute__((unused))void *arg);
// void pepa_emulator_shva_reader_thread_clean(void *arg);
// void *pepa_emulator_out_thread(__attribute__((unused))void *arg);
// void pepa_emulator_out_thread_cleanup(__attribute__((unused))void *arg);
// int32_t pepa_emulator_generate_buffer_buf(buf_t *buf, size_t buffer_size);
// void pepa_emulator_disconnect_mes(const char *name);
// void emu_set_int_signal_handler(void);
// void *controlling_thread(void *arg);

char       *several_messages    = "one\0two\0three\0";
size_t     several_messages_len = 14;

/*** EMU struct implimentation ****/


static const char *st_to_str(int st)
{
    switch (st) {
    case ST_NO:
        return "ST_NO";
    case ST_STOPPED:
        return "ST_STOPPED";
    case ST_STARTING:
        return "ST_STARTING";
    case ST_WAITING:
        return "ST_WAITING";
    case ST_RUNNING:
        return "ST_RUNNING";
    case ST_READY:
        return "ST_READY";
    case ST_RESET:
        return "ST_RESET";
    case ST_EXIT:
        return "ST_EXIT";
    default:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}
static emu_t *emu_t_allocate(int num_of_in, pepa_core_t *core)
{
    emu_t *emu = calloc(sizeof(emu_t), 1);
    TESTP(emu,  NULL);

    emu->in_number = num_of_in;

    /* Allocate pthread_t array for IN threads */
    emu->in_ids = calloc(sizeof(pthread_t), emu->in_number);
    emu->in_stat = calloc(sizeof(char), emu->in_number);

    if (NULL == emu->in_ids || NULL == emu->in_stat) {
        slog_error_l("Can not allocate emu_t: emu->in_ids = %p, emu->in_stat = %p",
                     emu->in_ids, emu->in_stat);
        free(emu);
        return NULL;
    }

    for (size_t idx = 0; idx < emu->in_number; idx++) {
        emu->in_ids[idx] = FD_CLOSED;
        emu->in_stat[idx] = ST_NO;
    }

    /* Set thread statuses to ST_NO */
    emu->shva_read_status = ST_NO;
    emu->shva_write_status = ST_NO;
    emu->shva_main_status = ST_NO;
    emu->out_status = ST_NO;

    emu->core = core;

    int rc = pthread_mutex_init(&emu->lock, NULL);
    if (rc) {
        slog_error_l("Can not init pthread lock, stop");
        abort();
    }
    return emu;
}

static int emu_t_free(emu_t *emu)
{

    TESTP(emu, -1);
    if (emu->in_number > 0 &&  emu->in_ids) {
        free(emu->in_ids);
    }

    free(emu);
    return 0;
}

static int emu_lock(emu_t *emu)
{
    TESTP(emu,  -1);
    return pthread_mutex_lock(&emu->lock);
}

static int emu_unlock(emu_t *emu)
{
    TESTP(emu,  -1);
    return pthread_mutex_unlock(&emu->lock);
}

static void emu_print_transition(const char *name, int prev_st,  int cur_st)
{
    slog_note_l("[EMU CONTROL] %s: %s -> %s", name, st_to_str(prev_st), st_to_str(cur_st));
}

static void emu_set_shva_read_status(emu_t *emu, const int status)
{
    emu_lock(emu);
    int prev_st = emu->shva_read_status;
    emu->shva_read_status = status;
    emu_unlock(emu);
    emu_print_transition("SHVA Read", prev_st, status);
}

static int emu_get_shva_read_status(emu_t *emu)
{
    emu_lock(emu);
    int st = emu->shva_read_status;
    emu_unlock(emu);

    return st;
}

static void emu_set_shva_write_status(emu_t *emu, const int status)
{
    emu_lock(emu);
    int prev_st = emu->shva_write_status;
    emu->shva_write_status = status;
    emu_unlock(emu);

    emu_print_transition("SHVA Write", prev_st, status);
}

static int emu_get_shva_write_status(emu_t *emu)
{
    emu_lock(emu);
    int st = emu->shva_write_status;
    emu_unlock(emu);

    return st;
}

static void emu_set_shva_main_status(emu_t *emu, const int status)
{
    emu_lock(emu);
    int prev_st = emu->shva_main_status;
    emu->shva_main_status = status;
    emu_unlock(emu);

    emu_print_transition("SHVA Socket", prev_st, status);
}

static int emu_get_shva_main_status(emu_t *emu)
{
    emu_lock(emu);
    int st = emu->shva_main_status;
    emu_unlock(emu);

    return st;
}

static void emu_set_out_status(emu_t *emu, const int status)
{
    emu_lock(emu);
    int prev_st = emu->out_status;
    emu->out_status = status;
    emu_unlock(emu);

    emu_print_transition("OUT", prev_st, status);
}

static int emu_get_out_status(emu_t *emu)
{
    emu_lock(emu);
    int st = emu->out_status;
    emu_unlock(emu);

    return st;
}

static void emu_set_in_status(emu_t *emu, const int in_num, const int status)
{
    char name[64];

    emu_lock(emu);
    int prev_st = emu->in_stat[in_num];
    emu->in_stat[in_num] = status;
    emu_unlock(emu);

    sprintf(name, "IN[%d]", in_num);
    emu_print_transition(name, prev_st, status);
}

static int emu_get_in_status(emu_t *emu, const int in_num)
{
    emu_lock(emu);
    int st = emu->in_stat[in_num];
    emu_unlock(emu);

    return st;
}

static void emu_set_all_in(emu_t *emu, const int status)
{
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        emu_set_in_status(emu, idx, status);
    }
}

static void emu_set_all_in_state_to_state(emu_t *emu, const int from, const int to)
{
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        if (from == emu_get_in_status(emu,  idx)) {}
        emu_set_in_status(emu, idx, to);
    }
}

static int emu_if_in_all_have_status(emu_t *emu, const int status)
{
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        if (status != emu_get_in_status(emu,  idx)) {
            return 0;
        }
    }

    return 1;
}

static int emu_if_in_any_have_status(emu_t *emu, const int status)
{
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        if (status == emu_get_in_status(emu,  idx)) {
            return 1;
        }
    }
    return 0;
}


/*** END OF EMU struct implimentation ****/

static void pthread_block_signals(const char *name)
{
    sigset_t set;
    sigfillset(&set);
    int rc = pthread_sigmask(SIG_SETMASK, &set, NULL);
    if (0 != rc) {
        slog_error_l("Could not set pthread signal blocking for thread %s", name);
    }
}

static void close_emulatior(void)
{
    uint32_t    i;
    pepa_core_t *core = pepa_get_core();
    /* Stop threads */
    slog_debug_l("Starting EMU clean");
    pepa_thread_cancel(core->out_thread.thread_id, "EMU OUT");
    for (i = 0; i < core->emu_in_threads; i++) {
        pepa_thread_cancel(in_thread_idx[i], "EMU IN");
    }
    pepa_thread_cancel(core->shva_thread.thread_id, "EMU SHVA");
    pepa_thread_cancel(control_thread_id, "EMU CONTROL");
    emu_t_free(emu);

    slog_debug_l("Finish EMU clean");
}

/* Catch Signal Handler function */
static void signal_callback_handler(int signum, __attribute__((unused)) siginfo_t *info, __attribute__((unused))void *extra)
{
    printf("Caught signal %d\n", signum);
    if (signum == SIGINT) {
        printf("Caught signal SIGINT: %d\n", signum);
        //pepa_back_to_disconnected_state_new();
        close_emulatior();
        exit(0);
    }
}

static void emu_set_int_signal_handler(void)
{
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = signal_callback_handler;
    sigaction(SIGINT, &action, NULL);
}

static void pepa_emulator_disconnect_mes(const char *name)
{
    slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_warn("EMU: Emulating %s disconnect", name);
    slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

static int32_t pepa_emulator_generate_buffer_buf(buf_t *buf, const size_t buffer_size)
{
    ret_t rc_buf;
    TESTP(buf, -1);
    buf->used = 0;

    if (buf->room < (buf_s64_t)buffer_size) {
        rc_buf = buf_test_room(buf, (buf_s64_t)buffer_size - buf->room);

        if (BUFT_OK != rc_buf) {
            slog_fatal_l("Could not allocate boof room: %d: %s", rc_buf, buf_error_code_to_string((int)rc_buf));
            abort();
        }
    }

    //slog_note_l("Buf allocated room");

    uint64_t rest = buffer_size;

    // slog_note_l("Starting copying into buf, asked len: %d", buffer_size);

    while (rest > 0) {
        uint64_t to_copy_size = PEPA_MIN(lorem_ipsum_len, (uint64_t)rest);
        rc_buf           = buf_add(buf, lorem_ipsum, (buf_s64_t)to_copy_size);

        if (rc_buf != BUFT_OK) {
            slog_fatal_l("Could not create text buffer: %s", buf_error_code_to_string((int)rc_buf));
            return -PEPA_ERR_BUF_ALLOCATION;
        }
        rest -= to_copy_size;
    }
    //slog_note_l("Finished copying into buf");
    // slog_note_l("Finished copying into buf: len = %d", buffer_size);

    return PEPA_ERR_OK;
}

static int32_t out_start_connection(void)
{
    int         sock;
    pepa_core_t *core = pepa_get_core();

    do {
        sock = pepa_open_connection_to_server(core->out_thread.ip_string->data, core->out_thread.port_int, __func__);
        if (sock < 0) {
            slog_error_l("Emu OUT: Could not connect to OUT (returned %d); |%s| ; waiting...", sock, strerror(errno));
            sleep(5);
        }
    } while (sock < 0);

    /* Set socket properties */
    pepa_set_tcp_timeout(sock);
    pepa_set_tcp_recv_size(core, sock);

    slog_debug_l("Established connection to OUT: %d", sock);
    return sock;
}

/**************** THREADS ***************************/

typedef struct {
    int epoll_fd;
    emu_t *emu;
} out_thread_args_t;

static void pepa_emulator_out_thread_cleanup(__attribute__((unused))void *arg)
{
    // int         *event_fd = (int *)arg;
    out_thread_args_t *args = arg;
    pepa_core_t       *core = pepa_get_core();
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    OUT CLEANUP                   $$$$$$$$$");

    int         rc_remove = epoll_ctl(args->epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);

    if (rc_remove) {
        slog_warn_l("[OUT] Could not remove RW socket %d from epoll set %d", core->sockets.out_write, args->epoll_fd);
    }

    pepa_reading_socket_close(core->sockets.out_write, "EMU OUT");

    /* Update status */
    emu_set_out_status(args->emu, ST_NO);

    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

/* Create 1 read socket to emulate OUT connection */
static void *pepa_emulator_out_thread(__attribute__((unused))void *arg)
{
    emu_t *emu = arg;
    TESTP_MES(emu,  NULL, "Thread argument is the NULL pointer");

    out_thread_args_t args;
    args.emu = emu;

    /* Update the status */
    emu_set_out_status(emu,  ST_STARTING);

    // int         epoll_fd;
    pthread_cleanup_push(pepa_emulator_out_thread_cleanup, (void *)&args);
    ssize_t     rc          = -1;
    pepa_core_t *core       = emu->core;
    int32_t     event_count;
    int32_t     i;

    uint64_t    reads       = 0;
    uint64_t    rx          = 0;

    pthread_block_signals("OUT");

    /* In this thread we read from socket as fast as we can */

    slog_note_l("[OUT] Going to allocate a new buf_t, size: %lu", core->emu_max_buf + 1);
    buf_t       *buf        = buf_new((buf_s64_t)core->emu_max_buf + 1);

    do {
        core->sockets.out_write      = out_start_connection();
        if (core->sockets.out_write < 0) {
            sleep(1);
            continue;
        }

        slog_note_l("[OUT] Opened out socket: fd: %d, port: %d", core->sockets.out_write, pepa_find_socket_port(core->sockets.out_write));

        struct epoll_event events[2];
        args.epoll_fd  = epoll_create1(EPOLL_CLOEXEC);

        if (0 != epoll_ctl_add(args.epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            slog_warn_l("    OUT: Tried to add sock fd = %d and failed", core->sockets.out_write);
            goto closeit;
        }

        /* Update the status */
        emu_set_out_status(emu, ST_WAITING);

        /* Entering the running loop */
        do {

            int status      = emu_get_out_status(emu);

            switch (status) {
            case ST_WAITING:
                usleep(10000);
                continue;
            case ST_STOPPED:
                goto stop_out_thread;
            case ST_RESET:
                goto closeit;
            default:
                break;
            }

            event_count = epoll_wait(args.epoll_fd, events, 1, 300000);
            int err = errno;
            /* Nothing to do, exited by timeout */
            if (0 == event_count) {
                continue;
            }

            /* Interrupted by a signal */
            if (event_count < 0 && EINTR == err) {
                continue;
            }

            if (event_count < 0) {
                slog_warn_l("    OUT: error on wait: %s", strerror(err));
                goto closeit;
            }

            for (i = 0; i < event_count; i++) {

                if (events[i].events & EPOLLRDHUP) {
                    slog_warn_l("    OUT: The remote disconnected: %s", strerror(err));
                    goto closeit;
                }

                if (events[i].events & EPOLLHUP) {
                    slog_warn_l("    OUT: Hung up happened: %s", strerror(err));
                    goto closeit;
                }

                /* Read from socket */
                if (events[i].events & EPOLLIN) {
                    do {
                        reads++;
                        rc = read(core->sockets.out_write, buf->data, core->emu_max_buf);
                        if (rc < 0) {
                            slog_warn_l("    OUT: Read/Write op between sockets failure: %s", strerror(errno));
                            goto closeit;
                        }
                        rx += (uint64_t)rc;

                        if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
                            slog_debug_l("     OUT: %-7lu reads, bytes: %-7lu, Kb: %-7lu", reads, rx, (rx / 1024));
                        }

                    } while (rc == buf->room);

                    continue;
                } /* for (i = 0; i < event_count; i++) */
            }

            /* Sometimes emulate broken connection: break the loop, then the socket will be closed */
            if (SHVA_SHOULD_EMULATE_DISCONNECT()) {
                slog_note_l("OUT      : EMULATING DISCONNECT");
                pepa_emulator_disconnect_mes("OUT");
                goto closeit;
            }

        } while (1); /* epoll loop */
    closeit:
        emu_set_out_status(emu,  ST_WAITING);

        rc = epoll_ctl(args.epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);

        if (rc) {
            slog_warn_l("[OUT]  Could not remove RW socket %d from epoll set %d", core->sockets.out_write, args.epoll_fd);
        }

        close(args.epoll_fd);
        // pepa_reading_socket_close(core->sockets.out_write, "OUT RW");
        pepa_socket_close(core->sockets.out_write, "OUT RW");
        sleep(3);
    } while (1);
    /* Now we can start send and recv */
stop_out_thread:
    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}

typedef struct {
    pthread_t shva_read_t;
    pthread_t shva_write_t;
} shva_args_t;

typedef struct {
    buf_t *buf;
    int eventfd;
    int sock_listen;
    emu_t *emu;
} shva_rw_thread_clean_t;


/** SHVA ***/


static void pepa_emulator_shva_reader_thread_clean(void *arg)
{
    pepa_core_t            *core  = pepa_get_core();
    shva_rw_thread_clean_t *cargs = (shva_rw_thread_clean_t *)arg;
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    SHVA READER CLEANUP           $$$$$$$$$");

    int rc = (int)buf_free(cargs->buf);
    if (rc) {
        slog_warn_l("Could not free buf_t: %s", buf_error_code_to_string(rc));
    }

    int rc_remove = epoll_ctl(cargs->eventfd, EPOLL_CTL_DEL, core->sockets.shva_rw, NULL);

    if (rc_remove) {
        slog_warn_l("%s: Could not remove socket %d from epoll set", "OUT RW", core->sockets.shva_rw);
    }

    close(cargs->eventfd);

    /* Update status */
    emu_set_shva_read_status(cargs->emu, ST_NO);

    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

static void *pepa_emulator_shva_reader_thread2(__attribute__((unused))void *arg)
{
    int   rc_remove;
    emu_t *emu      = arg;
    TESTP_MES(emu,  NULL,  "SHVA READER: Argument is NULL");

    shva_rw_thread_clean_t cargs;
    ssize_t                rc    = -1;
    pepa_core_t            *core = emu->core;
    int                    i;

    cargs.emu = emu;

    emu_set_shva_read_status(emu,  ST_STARTING);

    pthread_block_signals("SHVA-READ");

    uint64_t reads = 0;
    uint64_t tx    = 0;

    slog_debug("#############################################");
    slog_debug("##       THREAD <SHVA READER> IS STARTED   ##");
    slog_debug("#############################################");

    /* In this thread we read from socket as fast as we can */

    buf_t              *buf       = buf_new((buf_s64_t)core->emu_max_buf + 1);

    cargs.buf = buf;

    pthread_cleanup_push(pepa_emulator_shva_reader_thread_clean, (void *)&cargs);

    do { /* Create epoll */

        struct epoll_event events[20];
        int                epoll_fd   = epoll_create1(EPOLL_CLOEXEC);

        cargs.eventfd = epoll_fd;

        if (0 != epoll_ctl_add(epoll_fd, core->sockets.shva_rw, (EPOLLIN | EPOLLRDHUP | EPOLLHUP))) {
            slog_warn_l("[SHVA READ] Tried to add shva fd = %d and failed", core->sockets.shva_rw);
            pthread_exit(NULL);
        }

        emu_set_shva_read_status(emu, ST_WAITING);

        do { /* Read */
            int event_count;
            int err;

            int status      = emu_get_shva_read_status(emu);

            switch (status) {
            case ST_WAITING:
                usleep(10000);
                continue;
            case ST_STOPPED:
                goto stop_shva_reader;
            case ST_RESET:
                /* Close epoll socket, and wait until we can read again */
                goto reset_shva_read;
            default:
                break;
            }

            event_count = epoll_wait(epoll_fd, events, 20, 10);
            err         = errno;
            /* Nothing to do, exited by timeout */
            if (0 == event_count) {
                continue;
            }

            /* Interrupted by a signal */
            if (event_count < 0 && EINTR == err) {
                continue;
            }

            if (event_count < 0) {
                slog_warn_l("[SHVA READ] Error on wait: %s", strerror(err));
                close(epoll_fd);
                pthread_exit(NULL);
            }

            for (i = 0; i < event_count; i++) {
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    slog_info_l("[SHVA READ] THe remote disconnected: %s", strerror(err));
                    close(epoll_fd);
                    goto reset_shva_read;
                    // pthread_exit(NULL);
                }

                /* Read from socket */
                if (events[i].events & EPOLLIN) {
                    do {
                        reads++;
                        rc = read(core->sockets.shva_rw, buf->data, core->emu_max_buf);
                        if (rc < 0) {
                            slog_warn_l("[SHVA READ] Read/Write op between sockets failure: %s", strerror(errno));
                            close(epoll_fd);
                            // pthread_exit(NULL);
                            goto reset_shva_read;
                        }

                        if (0 == rc) {
                            slog_warn_l("[SHVA READ] Read op returned 0 bytes, an error: %s", strerror(errno));
                            close(epoll_fd);
                            // pthread_exit(NULL);
                            goto reset_shva_read;
                        }

                        tx += (uint64_t)rc;
                        if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
                            slog_debug_l("[SHVA READ] %-7lu reads, bytes: %-7lu, Kb: %-7lu", reads, tx, (tx / 1024));
                        }
                    } while (rc == buf->room);

                    continue;
                } /* for (i = 0; i < event_count; i++) */
            }

        } while (1); /* Read */

    reset_shva_read:

        rc_remove = epoll_ctl(cargs.eventfd, EPOLL_CTL_DEL, core->sockets.shva_rw, NULL);
        if (rc_remove) {
            slog_warn_l("[SHVA READ] Could not remove socket %d from epoll set", core->sockets.shva_rw);
        }
        close(cargs.eventfd);

    } while (1); /* Create epoll */
    /* Now we can start send and recv */
stop_shva_reader:
    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}

/* Create 1 read/write listening socket to emulate SHVA server */
#if 0 /* SEB */ /* 23/10/2024 */
static void *pepa_emulator_shva_reader_thread(__attribute__((unused))void *arg){
    emu_t *emu = arg;
    TESTP_MES(emu,  NULL,  "SHVA READER: Argument is NULL");

    shva_rw_thread_clean_t cargs;
    ssize_t                rc    = -1;
    pepa_core_t            *core = pepa_get_core();
    int                    i;

    cargs.emu = emu;

    emu_set_shva_read_status(emu,  ST_STARTING);

    pthread_block_signals("SHVA-READ");

    uint64_t reads = 0;
    uint64_t tx    = 0;

    slog_debug("#############################################");
    slog_debug("##       THREAD <SHVA READER> IS STARTED   ##");
    slog_debug("#############################################");

    /* In this thread we read from socket as fast as we can */

    buf_t              *buf       = buf_new((buf_s64_t)core->emu_max_buf + 1);

    cargs.buf = buf;

    pthread_cleanup_push(pepa_emulator_shva_reader_thread_clean, (void *)&cargs);

    struct epoll_event events[20];
    int                epoll_fd   = epoll_create1(EPOLL_CLOEXEC);

    cargs.eventfd = epoll_fd;

    if (0 != epoll_ctl_add(epoll_fd, core->sockets.shva_rw, (EPOLLIN | EPOLLRDHUP | EPOLLHUP))) {
        slog_warn_l("SHVA READ: Tried to add shva fd = %d and failed", core->sockets.shva_rw);
        pthread_exit(NULL);
    }

    shva_reader_up = 1;

    emu_set_shva_read_status(emu, ST_WAITING);

    do {
        int event_count;
        int err;

        int status      = emu_get_shva_read_status(emu);

        switch (status) {
            case ST_WAITING:
            usleep(10000);
            continue;
            case ST_STOPPED:
            goto stop_shva_reader;
            case ST_RESET:
            goto stop_shva_reader;
            default:
            break;
        }

        event_count = epoll_wait(epoll_fd, events, 20, 10);
        err         = errno;
        /* Nothing to do, exited by timeout */
        if (0 == event_count) {
            continue;
        }

        /* Interrupted by a signal */
        if (event_count < 0 && EINTR == err) {
            continue;
        }

        if (event_count < 0) {
            slog_warn_l("SHVA READ: error on wait: %s", strerror(err));
            close(epoll_fd);
            pthread_exit(NULL);
        }

        for (i = 0; i < event_count; i++) {
            if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
                slog_info_l("SHVA READ: THe remote disconnected: %s", strerror(err));
                close(epoll_fd);
                pthread_exit(NULL);
            }

            /* Read from socket */
            if (events[i].events & EPOLLIN) {
                do {
                    reads++;
                    rc = read(core->sockets.shva_rw, buf->data, core->emu_max_buf);
                    if (rc < 0) {
                        slog_warn_l("SHVA READ: Read/Write op between sockets failure: %s", strerror(errno));
                        close(epoll_fd);
                        pthread_exit(NULL);
                    }

                    if (0 == rc) {
                        slog_warn_l("SHVA READ: Read op returned 0 bytes, an error: %s", strerror(errno));
                        close(epoll_fd);
                        pthread_exit(NULL);
                    }

                    tx += (uint64_t)rc;
                    if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
                        slog_debug_l("SHVA READ: %-7lu reads, bytes: %-7lu, Kb: %-7lu", reads, tx, (tx / 1024));
                    }
                } while (rc == buf->room);

                continue;
            } /* for (i = 0; i < event_count; i++) */
        }

    } while (1);
    /* Now we can start send and recv */
    stop_shva_reader:
    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}
#endif /* SEB */ /* 23/10/2024 */

static void pepa_emulator_shva_writer_cleanup(void *arg)
{
    shva_rw_thread_clean_t *cargs = (shva_rw_thread_clean_t *)arg;

    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    SHVA WRITER CLEANUP           $$$$$$$$$");
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");

    emu_set_shva_write_status(cargs->emu, ST_NO);
}

static void *pepa_emulator_shva_writer_thread2(void *arg)
{
    emu_t *emu = arg;
    TESTP_MES(emu,  NULL,  "SHVA WRITER: Argument is NULL");

    pepa_core_t            *core        = emu->core;
    uint64_t               writes       = 0;
    int                    send_several = 1;

    shva_rw_thread_clean_t cargs;
    cargs.emu = emu;

    emu_set_shva_write_status(emu, ST_STARTING);

    pthread_cleanup_push(pepa_emulator_shva_writer_cleanup, &cargs);

    do {


        pthread_block_signals("SHVA-WRITE");

        slog_debug("#############################################");
        slog_debug("##       THREAD <SHVA WRITER> IS STARTED   ##");
        slog_debug("#############################################");

        /* In this thread we read from socket as fast as we can */

        emu_set_shva_write_status(emu,  ST_WAITING);
        do {
            ssize_t  rc;
            uint64_t rx     = 0;

            int      status = emu_get_shva_write_status(emu);
            switch (status) {
            case ST_WAITING:
                usleep(10000);
                continue;
            case ST_STOPPED:
                goto stop_shva_writer;
            case ST_RESET:
                /*
                 * We do not neet any reset for this thread;
                 * The 'shva socket' thread should reset the socket, and then
                 *  it can run again.
                 *  We also set our status to WAITING 
                 */
                emu_set_shva_write_status(emu,  ST_WAITING);
                usleep(10000);
                continue;
                // goto stop_shva_writer;
            default:
                break;
            }

            size_t   buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
            if (buf_size < core->emu_min_buf) {
                buf_size = core->emu_min_buf;
            }

            if (send_several > 0) {
                rc = write(core->sockets.shva_rw, several_messages, several_messages_len);
                send_several = 0;
            } else {
                rc = write(core->sockets.shva_rw, lorem_ipsum_buf->data, buf_size);
            }
            writes++;

            if (rc < 0) {
                slog_warn_l("[SHVA WRITE] Could not send buffer to SHVA, error: %s", strerror(errno));
                // goto stop_shva_writer;
                goto waiting_state;
            }

            if (0 == rc) {
                slog_warn_l("[SHVA WRITE] Send 0 bytes to SHVA, error: %s", strerror(errno));
                //goto stop_shva_writer;
                goto waiting_state;
                //usleep(100000);
                // continue;
            }

            rx += (uint64_t)rc;

            if (0 == (writes % RX_TX_PRINT_DIVIDER)) {
                slog_debug_l("[SHVA WRITE] %-7lu reads, bytes: %-7lu, Kb: %-7lu", writes, rx, (rx / 1024));
                send_several = 1;
            }

            if (core->emu_timeout > 0) {
                usleep(core->emu_timeout);
            }

        } while (1); /* Generating and sending data */
    waiting_state:
        emu_set_shva_write_status(emu, ST_WAITING);

    } while (1);

stop_shva_writer:

    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}

/* Create 1 read/write listening socket to emulate SHVA server */
#if 0 /* SEB */ /* 23/10/2024 */
static void *pepa_emulator_shva_writer_thread(__attribute__((unused))void *arg){
    emu_t *emu = arg;
    TESTP_MES(emu,  NULL,  "SHVA WRITER: Argument is NULL");

    pepa_core_t            *core        = pepa_get_core();
    uint64_t               writes       = 0;
    int                    send_several = 1;
    shva_rw_thread_clean_t cargs;
    cargs.emu = emu;

    pthread_cleanup_push(pepa_emulator_shva_writer_cleanup, &cargs);

    emu_set_shva_write_status(emu,  ST_STARTING);

    pthread_block_signals("SHVA-WRITE");

    slog_debug("#############################################");
    slog_debug("##       THREAD <SHVA WRITER> IS STARTED   ##");
    slog_debug("#############################################");

    /* In this thread we read from socket as fast as we can */

    emu_set_shva_write_status(emu,  ST_WAITING);
    do {
        ssize_t  rc;
        uint64_t rx     = 0;

        int      status = emu_get_shva_write_status(emu);
        switch (status) {
            case ST_WAITING:
            usleep(10000);
            continue;
            case ST_STOPPED:
            goto stop_shva_writer;
            case ST_RESET:
            goto stop_shva_writer;
            default:
            break;
        }

        size_t   buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
        if (buf_size < core->emu_min_buf) {
            buf_size = core->emu_min_buf;
        }

        shva_writer_up = 1;

// slog_note_l("SHVA WRITE: : Trying to write");
        if (send_several > 0) {
            rc = write(core->sockets.shva_rw, several_messages, several_messages_len);
            send_several = 0;
        } else {
            rc = write(core->sockets.shva_rw, lorem_ipsum_buf->data, buf_size);
        }
        writes++;

        if (rc < 0) {
            slog_warn_l("SHVA WRITE: : Could not send buffer to SHVA, error: %s", strerror(errno));
            goto stop_shva_writer;
        }

        if (0 == rc) {
            slog_warn_l("SHVA WRITE: Send 0 bytes to SHVA, error: %s", strerror(errno));
            goto stop_shva_writer;
            //usleep(100000);
            // continue;
        }

//slog_debug_l("SHVA WRITE: ~~~~>>> Written %d bytes", rc);
        rx += (uint64_t)rc;

        if (0 == (writes % RX_TX_PRINT_DIVIDER)) {
            slog_debug_l("SHVA WRITE: %-7lu reads, bytes: %-7lu, Kb: %-7lu", writes, rx, (rx / 1024));
            send_several = 1;
        }

        if (core->emu_timeout > 0) {
            usleep(core->emu_timeout);
        }

    } while (1); /* Generating and sending data */

    stop_shva_writer:

    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}
#endif /* SEB */ /* 23/10/2024 */

static void pepa_emulator_shva_thread_cleanup(__attribute__((unused))void *arg)
{
    shva_rw_thread_clean_t *cargs = (shva_rw_thread_clean_t *)arg;
    pepa_core_t            *core  = pepa_get_core();

    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    SHVA CLEANUP                  $$$$$$$$$");

    int rc_remove    = epoll_ctl(cargs->eventfd, EPOLL_CTL_DEL, cargs->sock_listen, NULL);

    if (rc_remove) {
        slog_warn_l("%s: Could not remove socket %d from epoll set", "OUT RW", core->sockets.shva_rw);
    }

    int32_t     rc           = pepa_socket_shutdown_and_close(cargs->eventfd, "EMU SHVA");
    if (PEPA_ERR_OK != rc) {
        slog_error_l("Could not close listening socket");
    }

    pepa_reading_socket_close(core->sockets.shva_rw, "SHVA RW");
    core->sockets.shva_rw = FD_CLOSED;

    emu_set_shva_main_status(cargs->emu, ST_NO);
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

#define EVENTS_NUM (5)
/* Create 1 read/write listening socket to emulate SHVA server */
#if 0 /* SEB */ /* 23/10/2024 */
static void *pepa_emulator_shva_thread(__attribute__((unused))void *arg){
    emu_t *emu = arg;
    TESTP_MES(emu,  NULL,  "SHVA MAIN: Argument is NULL");

    shva_rw_thread_clean_t cargs;
    pthread_t              shva_reader;
    pthread_t              shva_writer;
    int32_t                rc          = -1;
    pepa_core_t            *core       = pepa_get_core();
    struct sockaddr_in     s_addr;
    int                    sock_listen = FD_CLOSED;

    emu_set_shva_main_status(emu, ST_STARTING);

    pthread_block_signals("SHVA-MAIN");

    struct epoll_event events[EVENTS_NUM];
    int                epoll_fd           = epoll_create1(EPOLL_CLOEXEC);

    cargs.eventfd = epoll_fd;

    pthread_cleanup_push(pepa_emulator_shva_thread_cleanup, &cargs);


    emu_set_shva_main_status(emu, ST_WAITING);

    do {
        do {
            slog_note_l("[SHVA MAIN] OPEN LISTENING SOCKET");
            sock_listen = pepa_open_listening_socket(&s_addr, core->shva_thread.ip_string, core->shva_thread.port_int, 1, __func__);
            if (sock_listen < 0) {
                slog_note_l("[SHVA MAIN] Could not open listening socket, waiting...");
                usleep(1000);
            }
        } while (sock_listen < 0); /* Opening listening soket */

        cargs.sock_listen = sock_listen;

        slog_note_l("[SHVA MAIN] Opened listening socket");

        socklen_t addrlen      = sizeof(struct sockaddr);

        if (0 != epoll_ctl_add(epoll_fd, sock_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            close(epoll_fd);
            slog_fatal_l("[SHVA MAIN] Could not add listening socket to epoll");
            pthread_exit(NULL);
        }

        do {
            int i;

            /* The controlling thread can ask us to wait;
               It supervises all other threads and makes decisions */
            int status = emu_get_shva_main_status(emu);
            switch (status) {
                case ST_WAITING:
                usleep(10000);
                continue;
                case ST_STOPPED:
                goto shva_main_exit;
                case ST_RESET:
                goto reset;
                default:
                break;
            }

            int event_count = epoll_wait(epoll_fd, events, EVENTS_NUM, 100);

            /* No events, exited by timeout */
            if (0 == event_count) {

                /* Emulate socket closing */
                if (SHVA_SHOULD_EMULATE_DISCONNECT()) {
                    // slog_debug_l("SHVA: EMULATING DISCONNECT");
                    pepa_emulator_disconnect_mes("SHVA");
                    goto reset;
                }
            }

            /* Interrupted by a signal */
            if (event_count < 0 && EINTR == errno) {
                continue;
            }

            /* An error happened, we just terminate the thread */
            if (event_count < 0) {
                slog_fatal_l("[SHVA MAIN] error on wait: %s", strerror(errno));
                rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_listen, NULL);

                if (rc) {
                    slog_warn_l("[SHVA MAIN] Could not remove socket %d from epoll set %d", core->sockets.out_write, epoll_fd);
                }
                close(epoll_fd);
                pthread_exit(NULL);
            }


            for (i = 0; i < event_count; i++) {
                /* The listening socket is disconnected */
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    goto reset;
                }

                if (sock_listen == events[i].data.fd) {
                    slog_warn_l("[SHVA MAIN] Listening socket: got connection");


                    core->sockets.shva_rw = accept(sock_listen, &s_addr, &addrlen);
                    slog_note_l("[SHVA MAIN] ACCEPTED");
                    if (core->sockets.shva_rw < 0) {
                        slog_error_l("[SHVA MAIN] Could not accept: %s", strerror(errno));
                        core->sockets.shva_rw = FD_CLOSED;
                        continue;
                    }

                    pepa_set_tcp_timeout(core->sockets.shva_rw);
                    pepa_set_tcp_send_size(core, core->sockets.shva_rw);
                    pepa_set_tcp_recv_size(core, core->sockets.shva_rw);

                    /* Start read/write threads */
                    rc = pthread_create(&shva_writer, NULL, pepa_emulator_shva_writer_thread, emu);
                    if (rc < 0) {
                        slog_fatal_l("[SHVA MAIN] Could not create SHVA READ thread");
                        pthread_exit(NULL);
                    }
                    slog_fatal_l("[SHVA MAIN] Started SHVA READ thread");



                    rc = pthread_create(&shva_reader, NULL, pepa_emulator_shva_reader_thread, emu);
                    if (rc < 0) {
                        slog_fatal_l("[SHVA MAIN] Could not create SHVA WRITE thread");
                        pthread_exit(NULL);
                    }

                    slog_fatal_l("[SHVA MAIN] Started SHVA WRITE thread");
                }
            }

        } while (1);

        /* Emulate broken connection */
        reset:
        pthread_cancel(shva_reader);
        pthread_cancel(shva_writer);
        shva_reader_up = 0;
        shva_writer_up = 0;
        in_should_restart = core->emu_in_threads;

        /* Close rw socket */
        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_listen, NULL);

        if (rc) {
            slog_warn_l("[SHVA MAIN] Could not remove socket %d from epoll set %d", core->sockets.out_write, epoll_fd);
        }
        pepa_reading_socket_close(core->sockets.shva_rw, "SHVA RW");
        core->sockets.shva_rw = FD_CLOSED;

        rc = pepa_socket_shutdown_and_close(sock_listen, "SHVA MAIN");
        if (PEPA_ERR_OK != rc) {
            slog_error_l("[SHVA MAIN] Could not close listening socket");
        }
        sleep(5);
    } while (1); /* Opening connection and acceptiny */

/* Now we can start send and recv */

    shva_main_exit:

    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}
#endif /* SEB */ /* 23/10/2024 */


static void *pepa_emulator_shva_socket(__attribute__((unused))void *arg)
{
    emu_t *emu = arg;

    TESTP_MES(emu,  NULL,  "SHVA LISTEN: Argument is NULL");

    pepa_core_t            *core       = emu->core;

    shva_rw_thread_clean_t cargs;
    int32_t                rc          = -1;

    struct sockaddr_in     s_addr;
    int                    sock_listen = FD_CLOSED;

    emu_set_shva_main_status(emu, ST_STARTING);

    pthread_block_signals("SHVA-LISTEN");

    struct epoll_event events[EVENTS_NUM];
    int                epoll_fd           = epoll_create1(EPOLL_CLOEXEC);

    cargs.eventfd = epoll_fd;

    pthread_cleanup_push(pepa_emulator_shva_thread_cleanup, &cargs);


    emu_set_shva_main_status(emu, ST_WAITING);

    do {
        do {
            slog_note_l("[SHVA LISTEN] OPEN LISTENING SOCKET");
            sock_listen = pepa_open_listening_socket(&s_addr, core->shva_thread.ip_string, core->shva_thread.port_int, 1, __func__);
            if (sock_listen < 0) {
                slog_note_l("[SHVA LISTEN] Could not open listening socket, waiting...");
                usleep(1000);
            }
        } while (sock_listen < 0); /* Opening listening soket */


        core->sockets.shva_listen = sock_listen;

        slog_note_l("[SHVA LISTEN] Opened listening socket");

        socklen_t addrlen      = sizeof(struct sockaddr);

        if (0 != epoll_ctl_add(epoll_fd, sock_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            close(epoll_fd);
            slog_fatal_l("[SHVA LISTEN] Could not add listening socket to epoll");
            pthread_exit(NULL);
        }

        do {
            int i;

            /* The controlling thread can ask us to wait;
               It supervises all other threads and makes decisions */
            int status = emu_get_shva_main_status(emu);
            switch (status) {
            case ST_WAITING:
                usleep(10000);
                continue;
            case ST_STOPPED:
                goto shva_main_exit;
            case ST_RESET:
                goto reset;
            default:
                break;
            }

            int event_count = epoll_wait(epoll_fd, events, EVENTS_NUM, 100);

            /* No events, exited by timeout */
            if (0 == event_count) {

                /* Emulate socket closing */
                if (SHVA_SHOULD_EMULATE_DISCONNECT()) {
                    // slog_debug_l("SHVA: EMULATING DISCONNECT");
                    pepa_emulator_disconnect_mes("SHVA LISTEN");
                    goto reset;
                }
            }

            /* Interrupted by a signal */
            if (event_count < 0 && EINTR == errno) {
                continue;
            }

            /* An error happened, we close the listening socket and remove it from the epoll */
            if (event_count < 0) {
                slog_fatal_l("[SHVA LISTEN] error on wait: %s", strerror(errno));
                rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_listen, NULL);

                if (rc) {
                    slog_warn_l("[SHVA LISTEN] Could not remove socket %d from epoll set %d", core->sockets.out_write, epoll_fd);
                }
                goto reset;
            }

            /* If here, it means everything is OK and we have a connection on the listening socket, or a socket is broken */

            for (i = 0; i < event_count; i++) {
                /* The listening socket is disconnected */
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    goto reset;
                }

                if (sock_listen == events[i].data.fd && events[i].events == EPOLLIN) {

                    // slog_warn_l("[SHVA LISTEN] Listening socket: got connection");

                    /* If we have a RW socket opened, but there is another incoming connection is already established, we ignore this one */
                    if (FD_CLOSED != core->sockets.shva_rw) {
                        // slog_error_l("[SHVA LISTEN] We have RW socket but another connection is detected: we ignore it");
                        usleep(1000);
                        continue;
                    }

                    /* All right, we accept the connection */

                    core->sockets.shva_rw = accept(sock_listen, &s_addr, &addrlen);

                    slog_note_l("[SHVA LISTEN] ACCEPTED");
                    if (core->sockets.shva_rw < 0) {
                        slog_error_l("[SHVA LISTEN] Could not accept: %s", strerror(errno));
                        core->sockets.shva_rw = FD_CLOSED;
                        continue;
                    }

                    pepa_set_tcp_timeout(core->sockets.shva_rw);
                    pepa_set_tcp_send_size(core, core->sockets.shva_rw);
                    pepa_set_tcp_recv_size(core, core->sockets.shva_rw);

                    /* Add writing fd to epoll set */

                    /* Ready means we have both listening and RW sockest are ready */
                    emu_set_shva_main_status(emu,  ST_READY);
                }
            }

        } while (1);

        /* Emulate broken connection */
    reset:

        /* Close rw socket */
        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_listen, NULL);

        if (rc) {
            slog_warn_l("[SHVA LISTEN] Could not remove socket %d from epoll set %d", core->sockets.out_write, epoll_fd);
        }

        pepa_reading_socket_close(core->sockets.shva_rw, "SHVA RW");
        core->sockets.shva_rw = FD_CLOSED;

        rc = pepa_socket_shutdown_and_close(sock_listen, "SHVA MAIN");
        if (PEPA_ERR_OK != rc) {
            slog_error_l("[SHVA LISTEN] Could not close listening socket");
        }
        core->sockets.shva_listen = FD_CLOSED;
        /* We set the status to STOPPED; the control thread may need to restart SHVA read/write threads */
        emu_set_shva_main_status(emu,  ST_WAITING);

    } while (1); /* Opening connection and acceptiny */

/* Now we can start send and recv */

shva_main_exit:

    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}

#define RW_ITER_READ (10)
#define RW_ITER_WRITE (2)
static int pepa_emulator_shva_read(const int fd, size_t *rx, size_t *reads)
{
    int         cur   = 0;
    int         rc;
    pepa_core_t *core = pepa_get_core();
    buf_t       *buf  = buf_new((buf_s64_t)core->emu_max_buf + 1);

    for (int i = 0; i < RW_ITER_READ; i++) {
        /* Read from socket */
        *reads += 1;
        rc = read(fd, buf->data, core->emu_max_buf);
        if (rc < 0 && 0 == cur) {
            slog_warn_l("[SHVA READ] Read/Write op between sockets failure: %s", strerror(errno));
            return -1;
        }

        if (0 == rc && 0 == cur) {
            slog_warn_l("[SHVA READ] Read op returned 0 bytes, an error: %s", strerror(errno));
            return -2;
        }

        if (rc < 1 && cur > 0) {
            return cur;
        }

        *rx += (uint64_t)rc;
        cur += rc;
        if (*reads > 0 && 0 == (*reads % RX_TX_PRINT_DIVIDER)) {
            slog_debug_l("[SHVA READ] %-7lu reads, bytes: %-7lu, Kb: %-7lu", *reads, *rx, (*rx / 1024));
        }
    }

    return cur;
}

static int pepa_emulator_shva_write(const int fd, size_t *tx, size_t *writes)
{
    int         cur   = 0;
    ssize_t     rc;
    pepa_core_t *core = pepa_get_core();

    for (int i = 0; i < RW_ITER_WRITE; i++) {
        *writes += 1;
        if (FD_CLOSED == fd) {
            return 0;
        }

        size_t      buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
        if (buf_size < core->emu_min_buf) {
            buf_size = core->emu_min_buf;
        }

        rc = write(fd, lorem_ipsum_buf->data, buf_size);

        if (rc < 0 && 0 == cur) {
            slog_warn_l("[SHVA WRITE] Could not send buffer to SHVA, error: %s", strerror(errno));
            return -1;
        }

        if (0 == rc && 0 == cur) {
            slog_warn_l("[SHVA WRITE] Send 0 bytes to SHVA, error: %s", strerror(errno));
            return -2;
        }

        if (rc < 1 && cur > 0) {
            return cur;
        }

        *tx += (uint64_t)rc;

        if (*writes > 0 && 0 == (*writes % RX_TX_PRINT_DIVIDER)) {
            slog_debug_l("[SHVA WRITE] %-7lu reads, bytes: %-7lu, Kb: %-7lu", *writes, *tx, (*tx / 1024));
        }

        if (core->emu_timeout > 0) {
            usleep(core->emu_timeout);
        }
    }
    return cur;
}

#if 0 /* SEB */ /* 24/10/2024 */
int pepa_emulator_shva_loop(int fd, int *rx, int *tx, int *writes, int *reads){
    int rc = 0;

    rc = pepa_emulator_shva_write(fd, tx, writes);
    if (rc < 0) {
        return rc;
    }

    rc = pepa_emulator_shva_read(fd, rx, reads);
    if (rc < 0) {
        return rc;
    }

    return 0;
}
#endif /* SEB */ /* 24/10/2024 */

static int pepa_emu_shva_accept(int epoll_fd, int sock_listen, pepa_core_t *core)
{
    struct sockaddr_in s_addr;
    socklen_t          addrlen = sizeof(struct sockaddr);

    // slog_warn_l("[SHVA] Listening socket: got connection");

    /* If we have a RW socket opened, but there is another incoming connection is already established, we ignore this one */
    if (FD_CLOSED != core->sockets.shva_rw) {
        // slog_error_l("[SHVA] We have RW socket but another connection is detected: we ignore it");
        return -1;
    }

    /* All right, we accept the connection */

    core->sockets.shva_rw = accept(sock_listen, &s_addr, &addrlen);

    slog_note_l("[SHVA] ACCEPTED");
    if (core->sockets.shva_rw < 0) {
        slog_error_l("[SHVA] Could not accept: %s", strerror(errno));
        core->sockets.shva_rw = FD_CLOSED;
        return -1;
    }

    pepa_set_tcp_timeout(core->sockets.shva_rw);
    pepa_set_tcp_send_size(core, core->sockets.shva_rw);
    pepa_set_tcp_recv_size(core, core->sockets.shva_rw);

    /* Add writing fd to epoll set */
    if (0 != epoll_ctl_add(epoll_fd, core->sockets.shva_rw, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("[SHVA] Can not add RW socket to epoll set");
        return -1;
    }
    return 0;
}

static void *pepa_emulator_shva(void *arg)
{
    size_t rx     = 0;
    size_t tx     = 0;
    size_t reads  = 0;
    size_t writes = 0;
    emu_t  *emu   = arg;

    TESTP_MES(emu,  NULL,  "SHVA: Argument is NULL");

    pepa_core_t            *core       = emu->core;

    shva_rw_thread_clean_t cargs;
    int32_t                rc          = -1;

    struct sockaddr_in     s_addr;
    int                    sock_listen = FD_CLOSED;

    emu_set_shva_main_status(emu, ST_STARTING);

    pthread_block_signals("SHVA");

    struct epoll_event events[EVENTS_NUM];
    int                epoll_fd           = epoll_create1(EPOLL_CLOEXEC);

    cargs.eventfd = epoll_fd;

    pthread_cleanup_push(pepa_emulator_shva_thread_cleanup, &cargs);

    emu_set_shva_main_status(emu, ST_WAITING);

    do {
        do {
            slog_note_l("[SHVA] OPEN LISTENING SOCKET");
            sock_listen = pepa_open_listening_socket(&s_addr, core->shva_thread.ip_string, core->shva_thread.port_int, 1, __func__);
            if (sock_listen < 0) {
                slog_note_l("[SHVA] Could not open listening socket, waiting...");
                usleep(1000);
            }
        } while (sock_listen < 0); /* Opening listening soket */


        core->sockets.shva_listen = sock_listen;

        slog_note_l("[SHVA] Opened listening socket");

        if (0 != epoll_ctl_add(epoll_fd, sock_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            close(epoll_fd);
            slog_fatal_l("[SHVA] Could not add listening socket to epoll");
            pthread_exit(NULL);
        }

        do {
            /* The controlling thread can ask us to wait;
               It supervises all other threads and makes decisions */
            int status = emu_get_shva_main_status(emu);
            switch (status) {
            case ST_WAITING:
                usleep(10000);
                continue;
            case ST_STOPPED:
                goto shva_main_exit;
            case ST_RESET:
                goto reset;
            default:
                break;
            }

            int event_count = epoll_wait(epoll_fd, events, EVENTS_NUM, 1);

            /* No events, exited by timeout */
            if (0 == event_count) {

                /* Emulate socket closing */
                if (SHVA_SHOULD_EMULATE_DISCONNECT()) {
                    // slog_debug_l("SHVA: EMULATING DISCONNECT");
                    pepa_emulator_disconnect_mes("SHVA");
                    goto reset;
                }
            }

            /* Interrupted by a signal */
            if (event_count < 0 && EINTR == errno) {
                continue;
            }

            /* An error happened, we close the listening socket and remove it from the epoll */
            if (event_count < 0) {
                slog_fatal_l("[SHVA] error on wait: %s", strerror(errno));
                rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_listen, NULL);

                if (rc) {
                    slog_warn_l("[SHVA] Could not remove socket %d from epoll set %d", core->sockets.out_write, epoll_fd);
                }
                goto reset;
            }

            /* If here, it means everything is OK and we have a connection on the listening socket, or a socket is broken */

            for (int i = 0; i < event_count; i++) {

                /* The any socket is disconnected - reset all sockets */
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    goto reset;
                }

                /* IN event on listening, we should accept new connection */
                if (sock_listen == events[i].data.fd && events[i].events == EPOLLIN) {
                    rc = pepa_emu_shva_accept(epoll_fd, sock_listen, core);
                }  /* if (sock_listen == events[i].data.fd && events[i].events == EPOLLIN)*/

                /* If no RW socket, continue */
                if (FD_CLOSED == core->sockets.shva_rw) {
                    continue;
                }

                /* IN event on RW socket, read */
                if (core->sockets.shva_rw == events[i].data.fd && events[i].events == EPOLLIN) {
                    rc = pepa_emulator_shva_read(core->sockets.shva_rw, &rx, &reads);
                    if (rc < 0) {
                        slog_error_l("[SHVA] Can not read from RW socket, reset sockets");
                        goto reset;
                    }
                }

#if 0 /* SEB */ /* 24/10/2024 */
                if (core->sockets.shva_rw == events[i].data.fd && events[i].events == EPOLLOUT) {
                    rc = pepa_emulator_shva_write(core->sockets.shva_rw, &tx, &writes);
                    if (rc < 0) {
                        slog_error_l("[SHVA] Can not write to RW socket, reset sockets");
                        goto reset;
                    }
                }
#endif /* SEB */ /* 24/10/2024 */ 

            } /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) */
            /* Write to RW socket */

#if 1 /* SEB */ /* 24/10/2024 */
            /* If no RW socket, continue */
            if (FD_CLOSED == core->sockets.shva_rw) {
                continue;
            }

            rc = pepa_emulator_shva_write(core->sockets.shva_rw, &tx, &writes);
            if (rc < 0) {
                slog_error_l("[SHVA] Can not write to RW socket, reset sockets");
                goto reset;
            }
#endif /* SEB */ /* 24/10/2024 */ 

        } while (1);

        /* Emulate broken connection */
    reset:

        /* Close rw socket */
        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_listen, NULL);

        if (rc) {
            slog_warn_l("[SHVA] Could not remove Listen socket %d from epoll set %d", core->sockets.out_write, epoll_fd);
        }

        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, core->sockets.shva_rw, NULL);

        if (rc) {
            slog_warn_l("[SHVA] Could not remove RW socket %d from epoll set %d", core->sockets.out_write, epoll_fd);
        }

        pepa_reading_socket_close(core->sockets.shva_rw, "SHVA RW");
        core->sockets.shva_rw = FD_CLOSED;

        rc = pepa_socket_shutdown_and_close(sock_listen, "SHVA MAIN");
        if (PEPA_ERR_OK != rc) {
            slog_error_l("[SHVA] Could not close listening socket");
        }
        sock_listen = FD_CLOSED;
        core->sockets.shva_listen = FD_CLOSED;
        /* We set the status to STOPPED; the control thread may need to restart SHVA read/write threads */
        emu_set_shva_main_status(emu,  ST_WAITING);

    } while (1); /* Opening connection and acceptiny */

/* Now we can start send and recv */

shva_main_exit:

    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}

/*** IN ***/

static int32_t in_start_connection(void)
{
    pepa_core_t *core = pepa_get_core();
    int         sock;
    do {
        sock = pepa_open_connection_to_server(core->in_thread.ip_string->data, core->in_thread.port_int, __func__);
        if (sock < 0) {
            slog_note_l("Emu IN: Could not connect to IN; waiting...");
            sleep(5);
        }
    } while (sock < 0);

    pepa_set_tcp_timeout(sock);
    pepa_set_tcp_send_size(core, sock);

    return sock;
}

typedef struct {
    int fd;
    int my_num;
    emu_t *emu;

} in_thread_args_t;

static void pepa_emulator_in_thread_cleanup(__attribute__((unused))void *arg)
{
    in_thread_args_t *args = arg;

    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    IN_FORWARD CLEANUP            $$$$$$$$$");

    slog_note("IN: Going to close IN[%d] socket %d port %d", args->my_num, args->fd, pepa_find_socket_port(args->fd));

    pepa_reading_socket_close(args->fd, "EMU IN TRHEAD");


    emu_set_in_status(args->emu, args->my_num, ST_STOPPED);
    free(args);
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

/* If this flag is ON, all IN threads should emulate sicconnection and sleep for 5 seconds */
uint32_t in_thread_disconnect_all = 0;

/* Create 1 read/write listening socket to emulate SHVA server */
__attribute__((noreturn))
static void *pepa_emulator_in_thread(__attribute__((unused))void *arg)
{
    in_thread_args_t *args = arg;
    pepa_core_t      *core = pepa_get_core();
    // int         in_socket = FD_CLOSED;
    args->fd = FD_CLOSED;

    emu_set_in_status(args->emu, args->my_num, ST_STARTING);

    pthread_cleanup_push(pepa_emulator_in_thread_cleanup, args);

    // const int32_t *my_num     = (int32_t *)arg;
    char     my_name[32] = {0};

    uint64_t writes      = 0;
    uint64_t rx          = 0;

    emu_set_int_signal_handler();
    sprintf(my_name, "   IN[%.2d]", args->my_num);

    pthread_block_signals(my_name);

    /* In this thread we read from socket as fast as we can */


    do { /* Opening connection */
        /* Note: the in_start_connection() function can not fail; it blocking until connection opened */
        args->fd = in_start_connection();
        slog_warn_l("%s: Opened connection to IN: fd = %d, port: %d", my_name, args->fd, pepa_find_socket_port(args->fd));

        slog_info_l("%s: SHVA reader and writer are UP, continue", my_name);

        emu_set_in_status(args->emu, args->my_num, ST_WAITING);

        do { /* Run transfer */
            /* Disconnection of all IN thread is required */
            if (in_thread_disconnect_all > 0) {
                break;
            }

            /* The controlling thread can ask us to wait;
               It supervises all other threads and makes decisions */
            int status = emu_get_in_status(args->emu,  args->my_num);
            switch (status) {
            case ST_WAITING:
                usleep(10000);
                continue;
            case ST_STOPPED:
                slog_note_l("%s Thread asked to stop", my_name);
                goto in_stop;
            default:
                break;
            }

            size_t buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
            if (buf_size < core->emu_min_buf) {
                buf_size = core->emu_min_buf;
            }

            // slog_note_l("%s: Trying to write", , my_name);
            ssize_t rc = write(args->fd, lorem_ipsum_buf->data, buf_size);
            writes++;

            if (rc < 0) {
                slog_warn_l("%s: Could not send buffer to SHVA, error: %s (%d)", my_name, strerror(errno), errno);
                //break;
                goto reset_socket;
            }

            if (0 == rc) {
                slog_warn_l("%s: Send 0 bytes to SHVA, error: %s (%d)", my_name, strerror(errno), errno);
                // usleep(10000);
                //break;
                goto reset_socket;
            }


            //slog_debug_l("SHVA WRITE: ~~~~>>> Written %d bytes", rc);
            rx += (uint64_t)rc;

            if (0 == (writes % RX_TX_PRINT_DIVIDER)) {
                slog_debug_l("%s: %-7lu writes, bytes: %-7lu, Kb: %-7lu", my_name, writes, rx, (rx / 1024));
            }

            /* Emulate socket closing */
            if (SHOULD_EMULATE_DISCONNECT()) {
                pepa_emulator_disconnect_mes(my_name);
                goto reset_socket;
                // break;
            }

            /* Eulate ALL IN sockets disconnect */
            if (SHOULD_EMULATE_DISCONNECT()) {
                pepa_emulator_disconnect_mes("ALL IN");
                in_thread_disconnect_all = core->emu_in_threads;
                goto reset_socket;
                // break;
            }

            if (core->emu_timeout > 0) {
                usleep(core->emu_timeout);
            }


        } while (1); /* Generating and sending data */
    reset_socket:
        slog_warn_l("%s: Closing connection", my_name);
        close(args->fd);
        args->fd = FD_CLOSED;

        /* If 'disconnect all IN threads' counter is UP, decrease it */
        if (in_thread_disconnect_all > 0) {
            in_thread_disconnect_all--;
        }
        // sleep(5);
    } while (1);
in_stop:
    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}


/***********************************************************************************************************/
/**** REIMPLEMENTATION ****/
/***********************************************************************************************************/


/*** The OUT Thread ***/

static int start_out_thread(emu_t *emu)
{
    int        rc;
    slog_info_l("Starting OUT thread");
    rc = pthread_create(&emu->out_id, NULL, pepa_emulator_out_thread, emu);
    if (0 == rc) {
        slog_note_l("OUT thread created");
    } else {
        pepa_parse_pthread_create_error(rc);
        return -1;
    }
    slog_note_l("OUT thread is started");
    return 0;
}

static int kill_a_thread(pthread_t id, const char *name)
{
    int idx;
    int rc;
    slog_info_l("Stopping %s thread", name);
    rc = pthread_cancel(id);
    if (0 == rc) {
        slog_note_l("%s thread cancel request is sent", name);
    } else {
        pepa_parse_pthread_create_error(rc);
        return -1;
    }

    /* Wait until the thread is terminated */

    for (idx = 0; idx < 100; idx++) {
        usleep(1000);
        /* pthread_kill(X, 0) tests the pthread status. If the thread is running, it return 0  */
        if (0 != pthread_kill(id, 0)) {
            slog_note_l("%s thread is killed", name);
            return 0;
        }
    }

    slog_note_l("%s thread is NOT killed", name);
    return 1;
}

static int kill_out_thread(emu_t *emu)
{
    return kill_a_thread(emu->out_id,  "OUT");
}

#if 0 /* SEB */ /* 23/10/2024 */
static int kill_shva_reader(emu_t *emu){
    return kill_a_thread(emu->shva_read,  "SHVA_READ");
}
#endif /* SEB */ /* 23/10/2024 */

static int start_shva_socket(emu_t *emu)
{
    int        rc;
    slog_info_l("Starting SHVA socket thread");
    rc = pthread_create(&emu->shva_id, NULL, pepa_emulator_shva_socket, emu);
    if (0 == rc) {
        slog_note_l("SHVA socket thread created");
    } else {
        slog_note_l("SHVA socket thread was NOT created");
        pepa_parse_pthread_create_error(rc);
        return -1;
    }
    return 0;
}

static int start_shva(emu_t *emu)
{
    int        rc;
    slog_info_l("Starting SHVA socket thread");
    rc = pthread_create(&emu->shva_id, NULL, pepa_emulator_shva, emu);
    if (0 == rc) {
        slog_note_l("SHVA socket thread created");
    } else {
        slog_note_l("SHVA socket thread was NOT created");
        pepa_parse_pthread_create_error(rc);
        return -1;
    }
    return 0;
}

static int start_shva_reader(emu_t *emu)
{
    int        rc;
    slog_info_l("Starting SHVA reader thread");
    rc = pthread_create(&emu->shva_id, NULL, pepa_emulator_shva_reader_thread2, emu);
    if (0 == rc) {
        slog_note_l("SHVA reader thread created");
    } else {
        slog_note_l("SHVA reader thread was NOT created");
        pepa_parse_pthread_create_error(rc);
        return -1;
    }
    return 0;
}

static int start_shva_writer(emu_t *emu)
{
    int        rc;
    slog_info_l("Starting SHVA writer thread");
    rc = pthread_create(&emu->shva_id, NULL, pepa_emulator_shva_writer_thread2, emu);
    if (0 == rc) {
        slog_note_l("SHVA writer thread created");
    } else {
        slog_note_l("SHVA writer thread was NOT created");
        pepa_parse_pthread_create_error(rc);
        return -1;
    }
    return 0;
}


static int kill_shva_socket_thread(emu_t *emu)
{
    return kill_a_thread(emu->shva_id,  "SHVA SOCKET");
}

static int kill_shva(emu_t *emu)
{
    return kill_a_thread(emu->shva_id,  "SHVA SOCKET");
}

static int kill_shva_writer_thread(emu_t *emu)
{
    return kill_a_thread(emu->shva_write,  "SHVA WRITE");
}

static int kill_shva_reader_thread(emu_t *emu)
{
    return kill_a_thread(emu->shva_read,  "SHVA READ");
}

static int start_in_threads(emu_t *emu)
{
    size_t i;
    int    rc;

    for (i = 0; i < emu->in_number; i++) {
        if (ST_NO != emu->in_stat[i]) {
            continue;
        }

        slog_info_l("Starting IN[%lu] thread", i);
        in_thread_args_t *arg = calloc(sizeof(in_thread_args_t), 1);
        if (NULL == arg) {
            slog_error_l("Can not allocate IN arguments structure");
            abort();
        }

        arg->my_num = i;
        arg->emu = emu;

        rc = pthread_create(&emu->in_ids[i], NULL, pepa_emulator_in_thread, arg);
        if (0 == rc) {
            slog_note_l("IN[%lu] thread is created", i);
        } else {
            pepa_parse_pthread_create_error(rc);
            return -1;
        }
    }
    slog_note_l("IN threads are started");
    return 0;
}

static int kill_in_threads(emu_t *emu)
{
    size_t i;
    int    rc;

    for (i = 0; i < emu->in_number; i++) {
        slog_info_l("Stopping IN[%lu] thread", i);
        char name[64];
        sprintf(name, "%s[%lu]", "IN", i);
        rc = kill_a_thread(emu->in_ids[i],  name);

        if (0 == rc) {
            slog_note_l("IN[%lu] thread is stopped", i);
        } else {
            slog_error_l("IN[%lu] thread is NOT stopped", i);
        }
    }

    return 0;
}

static void in_st_print(emu_t *emu, char *buf)
{
    size_t      offset      = 0;
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        offset += sprintf(buf + offset, "[EMU CONTROL]: IN[%lu]       = %s\n", idx, st_to_str(emu->in_stat[idx]));
    }
}

#define PRINT_STATUSES (0)

static void emu_coltrol_pr_statuses(emu_t *emu)
{
    char        *buf                    = calloc(1024, 1);
    if (!PRINT_STATUSES) {
        return;
    }

    slog_note_l("[EMU CONTROL]: run");

    int st_out         = emu_get_out_status(emu);
    int st_shva_socket = emu_get_shva_main_status(emu);
    int st_shva_read   = emu_get_shva_read_status(emu);
    int st_shva_write  = emu_get_shva_write_status(emu);

    in_st_print(emu, buf);

    slog_note_l("\n[EMU CONTROL]: out         = %s\n"
                "[EMU CONTROL]: shva socket = %s\n"
                "[EMU CONTROL]: shva read   = %s\n"
                "[EMU CONTROL]: shva write  = %s\n"
                "%s"
                "[EMU CONTROL]: all IN NO?  = %d\n",
                st_to_str(st_out),
                st_to_str(st_shva_socket),
                st_to_str(st_shva_read),
                st_to_str(st_shva_write),
                buf,
                emu_if_in_all_have_status(emu, ST_NO));
}

#define L_TRACE_ENABLE (0)
#define L_TRACE() do{ if(L_TRACE_ENABLE)slog_note_l("[EMU CONTROL] Line Debug: Line %d",  __LINE__); }while(0)

#define POST_SLEEP_TIME (50000)
#define POST_SLEEP() do{usleep(POST_SLEEP_TIME);}while(0)
__attribute__((noreturn))
static void controll_state_machine(emu_t *emu)
{
    do {
        // char *buf = calloc(1024, 1);
        //int counter = 0;
        // sleep(1);
        usleep(50000);

        // slog_note_l("[EMU CONTROL]: run");

        int st_out         = emu_get_out_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 1");

        int st_shva_socket = emu_get_shva_main_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 2");

        // int st_shva_read   = emu_get_shva_read_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 3");

        // int st_shva_write  = emu_get_shva_write_status(emu);


        emu_coltrol_pr_statuses(emu);

        /* Start OUT thread */
        if (ST_NO == st_out) {
            slog_note_l("[EMU CONTROL] Starting OUT thread: ST_NO -> ST_WAITING");
            start_out_thread(emu);
            POST_SLEEP();
            //continue;
        }

        //slog_note_l("[EMU CONTROL] Line Debug: Line %d",  __LINE__);

        // slog_note_l("[EMU CONTROL]: 1");

        /* Start IN threads */
        if (emu_if_in_all_have_status(emu, ST_NO)) {
            slog_note_l("[EMU CONTROL] Starting running of INs: ST_NO -> ST_WAITING");
            start_in_threads(emu);
            POST_SLEEP();
            // continue;
        }

        /* Start SHVA socket thread; the main thread will start READ and WRITE threads */
        if (ST_NO == st_shva_socket) {
            slog_note_l("[EMU CONTROL] Starting SHVA main (socket) thread: ST_NO -> ST_WAITING");
            start_shva(emu);
            POST_SLEEP();
            //continue;
        }

        /* We DO NOT start here SHVA READ / WRITE; we start them only after INs are running */

        /*** EXCEPTIONS: stop / restart threads ***/

        /* If SHVA socket(s) are degraded, the Socket thread closes them and set statys RESET.
           We should restart both SHVA READ and WRITE threads, and also IN threads, and reset the Socket thread status */
        if (ST_RESET == st_shva_socket) {

            /* Reload IN threads */
            kill_in_threads(emu);
            POST_SLEEP();

            start_in_threads(emu);
            POST_SLEEP();

            emu_set_all_in(emu, ST_RUNNING);
            continue;
        }

        /* SHVA Socket: After reseting, set into WAITING state. It star depends on OUT thread state, we test it later */

        if (ST_READY == st_shva_socket) {
            emu_set_shva_main_status(emu, ST_WAITING);
            POST_SLEEP();
        }

        /* If SHVA socket reseted the socket and ready to continue, and writer and reader are existing, start them */

        /* OUT is WAITING, and SHVA is RUNNING: the OUT was restarted, set all SHVA threads to WAITING */

        if (ST_WAITING == st_out && ST_RUNNING == st_shva_socket) {
            //slog_note_l("[EMU CONTROL] Starting running of OUT: ST_WAITING -> ST_RUNNING");
            emu_set_shva_main_status(emu, ST_WAITING);
            POST_SLEEP();
        }

        /* 1. OUT thread is waiting and SHVA read is waiting: start OUT */

        if (ST_WAITING == st_out && ST_WAITING == st_shva_socket) {
            //slog_note_l("[EMU CONTROL] Starting running of OUT: ST_WAITING -> ST_RUNNING");
            emu_set_out_status(emu, ST_RUNNING);
            POST_SLEEP();
        }

        /* 2. OUT thread is running and SHVA socket is waiting: start SHVA socket, we are ready */

        if (ST_RUNNING == st_out && ST_WAITING == st_shva_socket) {
            //slog_note_l("[EMU CONTROL] Starting running of SHVA main: ST_WAITING -> ST_RUNNING");
            emu_set_shva_main_status(emu, ST_RUNNING);
            POST_SLEEP();
            // continue;
        }

        /* 4. SHVA write is running and any IN are waiting: start all IN threads */

        if (ST_RUNNING == st_shva_socket && emu_if_in_any_have_status(emu,  ST_WAITING)) {
            // slog_note_l("[EMU CONTROL] Starting running of INs: ST_WAITING -> ST_RUNNING");
            emu_set_all_in_state_to_state(emu,  ST_WAITING, ST_RUNNING);
            POST_SLEEP();
        }

        /* 5. SHVA write is stopped and any of IN is waiting: stop all IN threads */

        if (ST_STOPPED == st_shva_socket && emu_if_in_any_have_status(emu, ST_RUNNING)) {
            // slog_note_l("[EMU CONTROL] Stopping running of INs: ST_RUNNING -> ST_WAITING");
            emu_set_all_in_state_to_state(emu, ST_RUNNING, ST_WAITING);
            POST_SLEEP();
        }

        // slog_note_l("[EMU CONTROL]: end");

    } while (1);
    L_TRACE();
}


#if 0 /* SEB */ /* 24/10/2024 */
static void controll_state_machine_prev(emu_t *emu){
    do {
        // char *buf = calloc(1024, 1);
        //int counter = 0;
        sleep(1);

        // slog_note_l("[EMU CONTROL]: run");

        int st_out         = emu_get_out_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 1");

        int st_shva_socket = emu_get_shva_main_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 2");

        int st_shva_read   = emu_get_shva_read_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 3");

        int st_shva_write  = emu_get_shva_write_status(emu);

    #if 0 /* SEB */ /* 24/10/2024 */
        in_st_print(emu, buf);

        slog_note_l("\n[EMU CONTROL]: out         = %s\n"
                    "[EMU CONTROL]: shva socket = %s\n"
                    "[EMU CONTROL]: shva read   = %s\n"
                    "[EMU CONTROL]: shva write  = %s\n"
                    "%s"
                    "[EMU CONTROL]: all IN NO?  = %d\n",
                    st_to_str(st_out),
                    st_to_str(st_shva_socket),
                    st_to_str(st_shva_read),
                    st_to_str(st_shva_write),
                    buf,
                    emu_if_in_all_have_status(emu, ST_NO));
    #endif /* SEB */ /* 24/10/2024 */
        // emu_coltrol_pr_statuses(emu);

        /* Start OUT thread */
        if (ST_NO == st_out) {
            slog_note_l("[EMU CONTROL] Starting OUT thread: ST_NO -> ST_WAITING");
            start_out_thread(emu);
            continue;
        }

        //slog_note_l("[EMU CONTROL] Line Debug: Line %d",  __LINE__);
        L_TRACE();

        // slog_note_l("[EMU CONTROL]: 1");

        /* Start IN threads */
        if (emu_if_in_all_have_status(emu, ST_NO)) {
            slog_note_l("[EMU CONTROL] Starting running of INs: ST_NO -> ST_WAITING");
            start_in_threads(emu);
            continue;
        }
        L_TRACE();

        /* Start SHVA socket thread; the main thread will start READ and WRITE threads */
        if (ST_NO == st_shva_socket) {
            slog_note_l("[EMU CONTROL] Starting SHVA main (socket) thread: ST_NO -> ST_WAITING");
            start_shva_socket(emu);
            continue;
        }
        L_TRACE();

        /* We DO NOT start here SHVA READ / WRITE; we start them only after INs are running */

        /*** EXCEPTIONS: stop / restart threads ***/

        /* If SHVA socket(s) are degraded, the Socket thread closes them and set statys RESET.
           We should restart both SHVA READ and WRITE threads, and also IN threads, and reset the Socket thread status */
        if (ST_RESET == st_shva_socket) {
            if (ST_NO != st_shva_read) {
                emu_set_shva_read_status(emu,  ST_RESET);
                L_TRACE();
                // kill_shva_reader_thread(emu);
            }

            if (ST_NO != st_shva_write) {
                emu_set_shva_write_status(emu,  ST_RESET);
                L_TRACE();
                // kill_shva_writer_thread(emu);
            }

            /* Reload IN threads */
            kill_in_threads(emu);
            L_TRACE();
            POST_SLEEP();
            start_in_threads(emu);
            L_TRACE();
            POST_SLEEP();
            emu_set_all_in(emu, ST_RUNNING);
            L_TRACE();
            continue;
        }
        L_TRACE();


        /* SHVA Socket: After reseting, set into WAITING state. It star depends on OUT thread state, we test it later */

        if (ST_READY == st_shva_socket) {
            L_TRACE();
            emu_set_shva_main_status(emu, ST_WAITING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
        }
        L_TRACE();

        if (ST_RUNNING == st_shva_socket && ST_NO == st_shva_read) {
            L_TRACE();
            start_shva_reader(emu);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
        }
        L_TRACE();

        if (ST_RUNNING == st_shva_socket && ST_NO == st_shva_write) {
            L_TRACE();
            start_shva_writer(emu);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
        }
        L_TRACE();

        /* If SHVA socket reseted the socket and ready to continue, and writer and reader are existing, start them */

        if (ST_RUNNING == st_shva_socket && ST_WAITING == st_shva_read) {
            L_TRACE();
            emu_set_shva_read_status(emu, ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
        }
        L_TRACE();

        if (ST_RUNNING == st_shva_socket && ST_WAITING == st_shva_write) {
            L_TRACE();
            emu_set_shva_write_status(emu, ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
        }
        L_TRACE();

        /* If SHVA Socket is started, but writer is not running, start the SHVA writer */

        /* OUT is WAITING, and SHVA is RUNNING: the OUT was restarted, set all SHVA threads to WAITING */

        if (ST_WAITING == st_out && ST_RUNNING == st_shva_socket) {
            L_TRACE();
            //slog_note_l("[EMU CONTROL] Starting running of OUT: ST_WAITING -> ST_RUNNING");
            L_TRACE();
            emu_set_shva_read_status(emu, ST_WAITING);
            POST_SLEEP();
            emu_set_shva_write_status(emu, ST_WAITING);
            POST_SLEEP();
            emu_set_shva_main_status(emu, ST_WAITING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            //continue;
        }

        /* 1. OUT thread is waiting and SHVA read is waiting: start OUT */

        if (ST_WAITING == st_out && ST_WAITING == st_shva_socket) {
            L_TRACE();
            //slog_note_l("[EMU CONTROL] Starting running of OUT: ST_WAITING -> ST_RUNNING");
            L_TRACE();
            emu_set_out_status(emu, ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            //continue;
        }
        L_TRACE();

        /* 2. OUT thread is running and SHVA socket is waiting: start SHVA socket, we are ready */

        if (ST_RUNNING == st_out && ST_WAITING == st_shva_socket) {
            //slog_note_l("[EMU CONTROL] Starting running of SHVA main: ST_WAITING -> ST_RUNNING");
            L_TRACE();
            emu_set_shva_main_status(emu, ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            // continue;
        }
        L_TRACE();

        /* 3. OUT thread is running and SHVA main is waiting: start SHVA read */

        if (ST_RUNNING == st_out && ST_WAITING == st_shva_read) {
            //slog_note_l("[EMU CONTROL] Starting running of SHVA read: ST_WAITING -> ST_RUNNING");
            L_TRACE();
            emu_set_shva_read_status(emu, ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            //continue;
        }
        L_TRACE();

        /* 3. SHVA write is waiting: start SHVA write */

        if (ST_READY == st_shva_socket && ST_WAITING == st_shva_write && ST_WAITING == st_shva_read) {
            L_TRACE();
            //slog_note_l("[EMU CONTROL] Starting running of SHVA write: ST_WAITING -> ST_RUNNING");
            L_TRACE();
            emu_set_shva_main_status(emu,  ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            //continue;
        }
        L_TRACE();

        if (ST_RUNNING == st_shva_socket && ST_WAITING == st_shva_write) {
            L_TRACE();
            //slog_note_l("[EMU CONTROL] Starting running of SHVA write: ST_WAITING -> ST_RUNNING");
            L_TRACE();
            emu_set_shva_write_status(emu, ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            //continue;
        }
        L_TRACE();

        if (ST_RUNNING == st_shva_socket && ST_WAITING == st_shva_read) {
            L_TRACE();
            //slog_note_l("[EMU CONTROL] Starting running of SHVA write: ST_WAITING -> ST_RUNNING");
            L_TRACE();
            emu_set_shva_read_status(emu, ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            //continue;
        }
        L_TRACE();

        /* 4. SHVA write is running and any IN are waiting: start all IN threads */

        if (ST_RUNNING == st_shva_write && emu_if_in_any_have_status(emu,  ST_WAITING)) {
            L_TRACE();
            // slog_note_l("[EMU CONTROL] Starting running of INs: ST_WAITING -> ST_RUNNING");
            L_TRACE();
            // emu_set_all_in(emu, ST_RUNNING);
            emu_set_all_in_state_to_state(emu,  ST_WAITING, ST_RUNNING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            //continue;
        }
        L_TRACE();

        /* 5. SHVA write is stopped and any of IN is waiting: stop all IN threads */

        if (ST_STOPPED == st_shva_write && emu_if_in_any_have_status(emu, ST_RUNNING)) {
            L_TRACE();
            // slog_note_l("[EMU CONTROL] Stopping running of INs: ST_RUNNING -> ST_WAITING");
            L_TRACE();
            // emu_set_all_in(emu, ST_WAITING);
            emu_set_all_in_state_to_state(emu, ST_RUNNING, ST_WAITING);
            L_TRACE();
            POST_SLEEP();
            L_TRACE();
            //continue;
        }
        L_TRACE();

        // slog_note_l("[EMU CONTROL]: end");
        L_TRACE();

    } while (1);
    L_TRACE();
}
#endif /* SEB */ /* 24/10/2024 */
/**
 * @author Sebastian Mountaniol (21/10/2024)
 * @brief This thread is responsible to supervise all other threads.
 * @return void* Ignored
 * @details If a socket degraded or disconnect emulated, a thread might exit, or socket might become
 *  		irresposible. In this case, the control thread should detect it and restart this thread or even
 *  		all threads 
 */
static void *controlling_thread(void *arg)
{
    //int   rc;
    emu_t       *emu                = arg;

    if (NULL == emu) {
        slog_error_l("Got argument == NULL");
        pthread_exit(NULL);
    }

#if 0 /* SEB */ /* 23/10/2024 */
    pepa_core_t *core = emu->core;

    if (NULL == core) {
        slog_error_l("Got core == NULL");
        pthread_exit(NULL);
    }

    /* Let's start threads. First of all, we start OUT thread, then IN threads, and after it SHVA thread */

    /* Start OUT thread */
    rc = start_out_thread(emu);
    if (rc) {
        goto err_1;
    }

    /* Start IN threads */
    rc = start_in_threads(emu);
    if (rc) {
        goto err_1;
    }

    /* Start SHVA thread(s) */
    rc = start_shva_thread(emu);
    if (rc) {
        goto err_1;
    }
#endif /* SEB */ /* 23/10/2024 */

    /* Now we are going into endless loop, and supervise the threads status */
    controll_state_machine(emu);

//err_1:
    pthread_exit(NULL);
}

int main(int argi, char *argv[])
{
    slog_init("EMU", SLOG_FLAGS_ALL, 0);
    pepa_core_init();
    pepa_core_t *core = pepa_get_core();

    int32_t     rc    = pepa_parse_arguments(argi, argv);
    if (rc < 0) {
        slog_fatal_l("Could not parse");
        return rc;
    }

    pepa_config_slogger(core);
    lorem_ipsum_len = strlen(lorem_ipsum);
    lorem_ipsum_buf = buf_new((buf_s64_t)core->emu_max_buf);
    pepa_emulator_generate_buffer_buf(lorem_ipsum_buf, core->emu_max_buf);

    emu_set_int_signal_handler();

    srand(17);
    /* Somethime random can return predictable value in the beginning; we skip it */
    rc = rand();
    rc = rand();
    rc = rand();
    rc = rand();
    rc = rand();

    pepa_set_rlimit();

    emu = emu_t_allocate(core->emu_in_threads, core);
    if (NULL == emu) {
        slog_error_l("Can not allocate emu_t");
        abort();
    }

    rc = pthread_create(&control_thread_id, NULL, controlling_thread, emu);
    do {
        sleep(60);
    } while (1);
    return 0;


#if 0 /* SEB */ /* 23/10/2024 */
    if (NULL != core->out_thread.ip_string) {
        slog_info_l("Starting OUT thread");
        rc = pthread_create(&core->out_thread.thread_id, NULL, pepa_emulator_out_thread, NULL);
        if (0 == rc) {
            slog_note_l("SHVA thread is started");
        } else {
            pepa_parse_pthread_create_error(rc);
            return -PEPA_ERR_THREAD_CANNOT_CREATE;
        }
    }

    usleep(500000);

    if (NULL != core->shva_thread.ip_string) {
        slog_info_l("Starting SHVA thread");
        rc = pthread_create(&core->shva_thread.thread_id, NULL, pepa_emulator_shva_thread, NULL);
        if (0 == rc) {
            slog_note_l("SHVA thread is started");
        } else {
            pepa_parse_pthread_create_error(rc);
            return -PEPA_ERR_THREAD_CANNOT_CREATE;
        }
    }

    usleep(500000);

    uint32_t i;

    in_thread_idx = (pthread_t  *)calloc(sizeof(pthread_t), core->emu_in_threads);

    if (NULL != core->in_thread.ip_string) {
        for (i = 0; i < core->emu_in_threads; i++) {
            slog_info_l("Starting IN[%u] thread", i);
            //rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_emulator_in_thread, NULL);
            rc = pthread_create(&in_thread_idx[i], NULL, pepa_emulator_in_thread, &i);
            if (0 == rc) {
                slog_note_l("IN[%u] thread is started", i);
            } else {
                pepa_parse_pthread_create_error(rc);
                return -PEPA_ERR_THREAD_CANNOT_CREATE;
            }
        }
    }

    while (1) {
        sleep(600);
    }
#endif /* SEB */ /* 23/10/2024 */

    slog_warn_l("We should never be here, but this is the fact: we are here, my dear friend. This is the time to say farewell.");
    return (10);
}
