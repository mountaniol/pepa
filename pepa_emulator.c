#define _GNU_SOURCE
#include <pthread.h>
#include <sys/param.h>
#include <unistd.h> /* For read() */
#include <sys/epoll.h>
#include <errno.h>

#include "logger.h"
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

#define SHUTDOWN_DIVIDER (100003573)
#define SHVA_SHUTDOWN_DIVIDER (10000357)
#define SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHUTDOWN_DIVIDER))
#define SHVA_SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHVA_SHUTDOWN_DIVIDER))

#define RX_TX_PRINT_DIVIDER (1000000)

#define PEPA_MIN(a,b) ((a<b) ? a : b )

/* Keep here PIDs of IN threads */
pthread_t  *in_thread_idx;
uint32_t   number_of_in_threads = 4;

const char *lorem_ipsum         = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.\0";
uint64_t   lorem_ipsum_len      = 0;

buf_t      *lorem_ipsum_buf     = NULL;

void *pepa_emulator_in_thread(__attribute__((unused))void *arg);
void pepa_emulator_in_thread_cleanup(__attribute__((unused))void *arg);
int32_t         in_start_connection(void);
void *pepa_emulator_shva_thread(__attribute__((unused))void *arg);
void pepa_emulator_shva_thread_cleanup(__attribute__((unused))void *arg);
void *pepa_emulator_shva_writer_thread(__attribute__((unused))void *arg);
void *pepa_emulator_shva_reader_thread(__attribute__((unused))void *arg);
void pepa_emulator_shva_reader_thread_clean(void *arg);
void *pepa_emulator_out_thread(__attribute__((unused))void *arg);
void pepa_emulator_out_thread_cleanup(__attribute__((unused))void *arg);
int32_t pepa_emulator_generate_buffer_buf(buf_t *buf, size_t buffer_size);
void pepa_emulator_disconnect_mes(const char *name);
void emu_set_int_signal_handler(void);

char   *several_messages    = "one\0two\0three\0";
size_t several_messages_len = 14;

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

void emu_set_int_signal_handler(void)
{
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = signal_callback_handler;
    sigaction(SIGINT, &action, NULL);
}

void pepa_emulator_disconnect_mes(const char *name)
{
    slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_warn("EMU: Emulating %s disconnect", name);
    slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

int32_t pepa_emulator_generate_buffer_buf(buf_t *buf, const size_t buffer_size)
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

    if (rc_buf > 0) {

        buf->data[buf->used - 5] = '@';
    }

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

void pepa_emulator_out_thread_cleanup(__attribute__((unused))void *arg)
{
    int         *event_fd = (int *)arg;
    pepa_core_t *core     = pepa_get_core();
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    OUT CLEANUP                   $$$$$$$$$");

    int         rc_remove = epoll_ctl(*event_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);

    if (rc_remove) {
        slog_warn_l("%s: Could not remove socket %d from epoll set %d", "OUT RW", core->sockets.out_write, *event_fd);
    }

    pepa_reading_socket_close(core->sockets.out_write, "EMU OUT");
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

/* Create 1 read socket to emulate OUT connection */
__attribute__((noreturn))
void *pepa_emulator_out_thread(__attribute__((unused))void *arg)
{
    int         epoll_fd;
    pthread_cleanup_push(pepa_emulator_out_thread_cleanup, (void *)&epoll_fd);
    ssize_t     rc          = -1;
    pepa_core_t *core       = pepa_get_core();
    int32_t     event_count;
    int32_t     i;

    uint64_t    reads       = 0;
    uint64_t    rx          = 0;

    pthread_block_signals("OUT");

    /* In this thread we read from socket as fast as we can */

    buf_t       *buf        = buf_new((buf_s64_t)core->emu_max_buf + 1);

    do {
        core->sockets.out_write      = out_start_connection();
        if (core->sockets.out_write < 0) {
            sleep(1);
            continue;
        }

        slog_note_l("Opened out socket: fd: %d, port: %d", core->sockets.out_write, pepa_find_socket_port(core->sockets.out_write));

        struct epoll_event events[2];
        epoll_fd  = epoll_create1(EPOLL_CLOEXEC);

        if (0 != epoll_ctl_add(epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            slog_warn_l("    OUT: Tried to add sock fd = %d and failed", core->sockets.out_write);
            goto closeit;
        }

        do {
            event_count = epoll_wait(epoll_fd, events, 1, 300000);
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
                        memset(buf->data, 0, buf->room);
                        rc = read(core->sockets.out_write, buf->data, core->emu_max_buf);
                        if (rc < 0) {
                            slog_warn_l("    OUT: Read/Write op between sockets failure: %s", strerror(errno));
                            goto closeit;
                        }
                        rx += (uint64_t)rc;

                        // buf->data[rc] = 0;
                        // printf("OUT: Received: |%s|\n", buf->data);


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
        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);

        if (rc) {
            slog_warn_l("%s: Could not remove socket %d from epoll set %d", "OUT RW", core->sockets.out_write, epoll_fd);
        }

        close(epoll_fd);
        pepa_reading_socket_close(core->sockets.out_write, "EMU OUT");
    } while (1);
    /* Now we can start send and recv */
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
} shva_rw_thread_clean_t;

void pepa_emulator_shva_reader_thread_clean(void *arg)
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
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

/* Create 1 read/write listening socket to emulate SHVA server */
__attribute__((noreturn))
void *pepa_emulator_shva_reader_thread(__attribute__((unused))void *arg)
{
    shva_rw_thread_clean_t cargs;
    ssize_t                rc    = -1;
    pepa_core_t            *core = pepa_get_core();
    int                    i;

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

    do {
        int event_count = epoll_wait(epoll_fd, events, 20, 10);
        int err         = errno;
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
                    memset(buf->data, 0, buf->room);
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

                    // buf->data[rc] = 0;
                    // printf("SHVA: Received: |%s|\n", buf->data);

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
    slog_debug_l("SHVA READ: Exiting READ THREAD");
    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}


/* Create 1 read/write listening socket to emulate SHVA server */
__attribute__((noreturn))
void *pepa_emulator_shva_writer_thread(__attribute__((unused))void *arg)
{
    pepa_core_t *core        = pepa_get_core();
    uint64_t    writes       = 0;
    int         send_several = 1;

    pthread_block_signals("SHVA-WRITE");

    slog_debug("#############################################");
    slog_debug("##       THREAD <SHVA WRITER> IS STARTED   ##");
    slog_debug("#############################################");

    /* In this thread we read from socket as fast as we can */

    do {
        ssize_t  rc;
        uint64_t rx       = 0;
        size_t   buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
        if (buf_size < core->emu_min_buf) {
            buf_size = core->emu_min_buf;
        }

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
            pthread_exit(NULL);
        }

        if (0 == rc) {
            slog_warn_l("SHVA WRITE: Send 0 bytes to SHVA, error: %s", strerror(errno));
            pthread_exit(NULL);
            //usleep(100000);
            continue;
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

    slog_debug_l("SHVA WRITE: Exiting WRITE THREAD");
    pthread_exit(NULL);
}

void pepa_emulator_shva_thread_cleanup(__attribute__((unused))void *arg)
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
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

#define EVENTS_NUM (10)
/* Create 1 read/write listening socket to emulate SHVA server */
__attribute__((noreturn))
void *pepa_emulator_shva_thread(__attribute__((unused))void *arg)
{
    shva_rw_thread_clean_t cargs;
    pthread_t              shva_reader = 0xDEADBEEF;
    pthread_t              shva_writer = 0xDEADBEEF;;
    int32_t                rc          = -1;
    pepa_core_t            *core       = pepa_get_core();
    struct sockaddr_in     s_addr;
    int                    sock_listen = FD_CLOSED;

    pthread_block_signals("SHVA-MAIN");

    struct epoll_event events[EVENTS_NUM];
    int                epoll_fd           = epoll_create1(EPOLL_CLOEXEC);

    cargs.eventfd = epoll_fd;

    pthread_cleanup_push(pepa_emulator_shva_thread_cleanup, &cargs);

    do {
        do { /* Open SHVA Listening socket */
            slog_note_l("EMU SHVA: OPEN LISTENING SOCKET");
            sock_listen = pepa_open_listening_socket(&s_addr, core->shva_thread.ip_string, core->shva_thread.port_int, 1, __func__);
            if (sock_listen < 0) {
                slog_note_l("EMU SHVA: Could not open listening socket, waiting...");
                //usleep(1000);
                sleep(1);
            }
        } while (sock_listen < 0); /* Opening listening soket */

        cargs.sock_listen = sock_listen;

        slog_note_l("EMU SHVA: Opened listening socket");

        socklen_t addrlen      = sizeof(struct sockaddr);

        if (0 != epoll_ctl_add(epoll_fd, sock_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            close(epoll_fd);
            slog_fatal_l("EMU SHVA: Could not add listening socket to epoll");
            pthread_exit(NULL);
        }

        do {
            int i;

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
                slog_fatal_l("EMU SHVA: error on wait: %s", strerror(errno));
                rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_listen, NULL);

                if (rc) {
                    slog_warn_l("EMU SHVA: %s: Could not remove socket %d from epoll set %d", "OUT RW", core->sockets.out_write, epoll_fd);
                }
                close(epoll_fd);
                pthread_exit(NULL);
            }


            /* Analyze events */
            for (i = 0; i < event_count; i++) {

                /* The listening socket is disconnected */
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    goto reset;
                }

                if (sock_listen == events[i].data.fd) {
                    slog_warn_l("EMU SHVA: Listening socket: got connection");

                    core->sockets.shva_rw = accept(sock_listen, &s_addr, &addrlen);
                    slog_note_l("EMU SHVA: EXITED FROM ACCEPTING");
                    if (core->sockets.shva_rw < 0) {
                        slog_error_l("EMU SHVA: Could not accept: %s", strerror(errno));
                        core->sockets.shva_rw = FD_CLOSED;
                        continue;
                    }

                    pepa_set_tcp_timeout(core->sockets.shva_rw);
                    pepa_set_tcp_send_size(core, core->sockets.shva_rw);
                    pepa_set_tcp_recv_size(core, core->sockets.shva_rw);

                    /* Start read/write threads */
                    rc = pthread_create(&shva_writer, NULL, pepa_emulator_shva_writer_thread, NULL);
                    if (rc < 0) {
                        slog_fatal_l("EMU SHVA: Could not create SHVA READ thread");
                        pthread_exit(NULL);
                    }

                    rc = pthread_create(&shva_reader, NULL, pepa_emulator_shva_reader_thread, NULL);
                    if (rc < 0) {
                        slog_fatal_l("EMU SHVA: Could not create SHVA WRITE thread");
                        pthread_exit(NULL);
                    }

                    usleep(1000);
                }
            }

            if (shva_reader != 0xDEADBEEF && 0 != pthread_kill(shva_reader, 0)) {
                slog_note_l("EMU SHVA: Reading thread is dead; restart both reader and writer");
                goto reset;
            }

            if (shva_writer != 0xDEADBEEF && 0 != pthread_kill(shva_writer, 0)) {
                slog_note_l("EMU SHVA: Reading thread is dead; restart both reader and writer");
                goto reset;
            }

            /* We also test that both reader and writer threads are alive.
               If any of them is dead, we restart both */


        } while (1);

        /* Emulate broken connection */
    reset:
        pthread_cancel(shva_reader);
        pthread_cancel(shva_writer);

        shva_reader = 0xDEADBEEF;
        shva_writer = 0xDEADBEEF;

        /* Close rw socket */
        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_listen, NULL);

        if (rc) {
            slog_warn_l("EMU SHVA: %s: Could not remove socket %d from epoll set %d", "OUT RW", core->sockets.out_write, epoll_fd);
        }
        pepa_reading_socket_close(core->sockets.shva_rw, "EMU SHVA: RW");
        core->sockets.shva_rw = FD_CLOSED;

        rc = pepa_socket_shutdown_and_close(sock_listen, "EMU SHVA: LISTEN");
        if (PEPA_ERR_OK != rc) {
            slog_error_l("SHVA: Could not close listening socket");
        }

        sleep(5);
    } while (1); /* Opening connection and acceptiny */

/* Now we can start send and recv */

    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}

int32_t         in_start_connection(void)
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

void pepa_emulator_in_thread_cleanup(__attribute__((unused))void *arg)
{
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    IN_FORWARD CLEANUP            $$$$$$$$$");
    int *fd = (int *)arg;
    slog_note("IN: Going to close socket %d port %d", *fd, pepa_find_socket_port(*fd));
    pepa_reading_socket_close(*fd, "EMU IN TRHEAD");
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

/* If this flag is ON, all IN threads should emulate sicconnection and sleep for 5 seconds */
uint32_t in_thread_disconnect_all = 0;

/* Create 1 read/write listening socket to emulate SHVA server */
__attribute__((noreturn))
void *pepa_emulator_in_thread(__attribute__((unused))void *arg)
{
    int         fails     = 0;
    pepa_core_t *core     = pepa_get_core();
    int         in_socket = FD_CLOSED;
    pthread_cleanup_push(pepa_emulator_in_thread_cleanup, &in_socket);

    const int32_t *my_num     = (int32_t *)arg;
    char          my_name[32] = {0};

    uint64_t      writes      = 0;
    uint64_t      rx          = 0;

    emu_set_int_signal_handler();
    sprintf(my_name, "   IN[%.2d]", *my_num);

    pthread_block_signals(my_name);

    /* In this thread we read from socket as fast as we can */

    do {
        fails = 0;

        /* Note: the in_start_connection() function can not fail; it blocking until connection opened */
        in_socket = in_start_connection();
        slog_info_l("%s: Opened connection to IN: fd = %d, port: %d", my_name, in_socket, pepa_find_socket_port(in_socket));
        sleep(1);

        do {

            /* Disconnection of all IN thread is required */
            if (in_thread_disconnect_all > 0) {
                break;
            }

            size_t buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
            if (buf_size < core->emu_min_buf) {
                buf_size = core->emu_min_buf;
            }


            // slog_note_l("%s: Trying to write", , my_name);
            ssize_t rc = write(in_socket, lorem_ipsum_buf->data, buf_size);
            writes++;

            if (rc < 0) {
                fails++;
                // slog_warn_l("%s: Could not send buffer to SHVA, error: %s", my_name, strerror(errno));
                slog_warn_l("%s: Could not send buffer to SHVA, fail [%d] error: %s", my_name, fails, strerror(errno));
                usleep(10000);
                // break;
            }

            if (0 == rc) {
                slog_warn_l("%s: Send 0 bytes to SHVA, error: %s", my_name, strerror(errno));
                usleep(10000);
                fails++;
                // break;
                continue;
            }

            if (rc > 0 && fails > 0) {
                fails = 0;
            }

            if (3 == fails) {
                slog_warn_l("%s: Could not send buffer to SHVA %d times, error: %s, interrupting socket", my_name, fails, strerror(errno));
                break;
            }

            //slog_debug_l("SHVA WRITE: ~~~~>>> Written %d bytes", rc);
            rx += (uint64_t)rc;

            if (0 == (writes % RX_TX_PRINT_DIVIDER)) {
                slog_debug_l("%s: %-7lu writes, bytes: %-7lu, Kb: %-7lu", my_name, writes, rx, (rx / 1024));
            }

            /* Emulate socket closing */
            if (SHOULD_EMULATE_DISCONNECT()) {
                pepa_emulator_disconnect_mes(my_name);
                break;
            }

            /* Eulate ALL IN sockets disconnect */
            if (SHOULD_EMULATE_DISCONNECT()) {
                pepa_emulator_disconnect_mes("ALL IN");
                in_thread_disconnect_all = core->emu_in_threads;
                break;
            }

            if (core->emu_timeout > 0) {
                usleep(core->emu_timeout);
            }


        } while (1); /* Generating and sending data */

        slog_warn_l("%s: Closing connection", my_name);
        close(in_socket);
        in_socket = FD_CLOSED;

        /* If 'disconnect all IN threads' counter is UP, decrease it */
        if (in_thread_disconnect_all > 0) {
            in_thread_disconnect_all--;
        }
        sleep(5);
    } while (1);

    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}

int main(int argi, char *argv[])
{

    int32_t rc = logger_start();
    if (rc) {
        printf("Could not start PEPA logger\n");
        return -1;
    }

    llog_r("PEPA Logger: %s int: %d",  "test", 22);

    slog_init("EMU", SLOG_FLAGS_ALL, 0);
    pepa_core_init();
    pepa_core_t *core = pepa_get_core();

    rc    = pepa_parse_arguments(argi, argv);
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

    slog_warn_l("We should never be here, but this is the fact: we are here, my dear friend. This is the time to say farewell.");
    return (10);
}
