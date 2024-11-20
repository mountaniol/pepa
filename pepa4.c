#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_state_machine.h"
#include "pepa4.h"
#include "pepa_in_reading_sockets.h"
#include "pepa_utils.h"
#include "queue.h"
#include "pepa_errors.h"
#include "pepa_types.h"

enum pepa4_go_states2 {
    START_START = 2000, /**< Start state, executed once  */
    START_OUT_LISTEN, /**< Start OUT listening socket */
    START_SOCKETS = START_OUT_LISTEN, /**< Start OUT listening socket */
    START_IN_LISTEN, /**< Only Start IN listening socket */
    START_OUT_RW, /**< Wait OUT connected */
    START_IN_RW, /**< Strat IN RW sockets */
    START_SHVA, /**< Start SHVA socket */
    START_DONE, /**< All done, return to caller */
    START_END, /**< PEPA must exit */
};

/**
 * @author Sebastian Mountaniol (1/9/24)
 * @brief Internal statuses 
 * @details 
 */
enum pepa4_errors {
    TE_RESTART = 3000, /**< All sockets need full restart */
    TE_RESET, /**< Sockets need partial restart, listening sockets are OK */
    TE_IN_RESTART, /**< IN sockets need full restart */
    TE_IN_REMOVED, /**< On of IN listening sockets was removed */
};


/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief The first step of the state machine. Allocate all
 *  	  needded structures, create epoll file descriptor
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 */
static void pepa4_start(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: START PHASE ///");
    core->buffer = calloc(core->internal_buf_size, 1);

    if (NULL == core->buffer) {
        slog_error_l("Can not allocate a transfering buffer, stopped");
        slog_error_l("/// ERROR: START PHASE ///");
        abort();
    }

    slog_note_l("Allocated internal buffer: %p size: %u", core->buffer, core->internal_buf_size);
    core->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (core->epoll_fd < 0) {
        slog_error_l("Can not create eventfd file descriptor, stopped");
        free(core->buffer);
        slog_error_l("/// ERROR: START PHASE ///");
        abort();
    }

    /* TODO: Instead of 1024 make it configurable */
    pepa_in_reading_sockets_allocate(core, PEPA_IN_SOCKETS);

    slog_note_l("Finished 'start' phase");
    slog_note_l("/// FINISH: START PHASE ///");
}

static void pepa_remove_socket_from_epoll(pepa_core_t *core, const int fd, const char *fd_name, const char *file, const int line)
{
    /* Remove the broken IN read socket from the epoll */
    int rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, fd, NULL);

    if (rc_remove) {
        switch (errno) {
            case ENOENT:
                slog_warn_l("Can not remove from EPOLL: The socket [%s] (FD = %d) (%s +%d) is already removed from the epoll set", fd_name, fd, file, line);
                break;
            case EBADF:
                slog_warn_l("Can not remove from EPOLL: The socket [%s] (FD = %d) (%s +%d) is a bad file descriptor", fd_name, fd, file, line);
                break;
            default:
                slog_warn_l("Can not remove from EPOLL: The socket [%s] (FD = %d) (%s +%d) can not be removed, the error is: %s", fd_name, fd, file, line, strerror(errno));
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
#define EVENTS_NUM (1)

/* epoll max timeout, milliseconds */
#define EPOLL_TIMEOUT (10)


static int pepa_process_exceptions2(pepa_core_t *core, const struct epoll_event events_array[], const int event_count)
{
    // int rc_remove;
    int i;
    for (i = 0; i < event_count; i++) {
        const uint32_t events = events_array[i].events;
        const int fd = events_array[i].data.fd;

        // if (!(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
        if (PEPA_ERR_OK == pepa_epoll_fd_ok(events)) {
            continue;
        }

        /*** The remote side is disconnected ***/

        /* If one of the read/write sockets is diconnected, stop */
        if (events & (EPOLLRDHUP)) {
            /* IN listener socket is degraded */
            if (core->sockets.in_listen == fd) {
                slog_warn_l("IN socket: remote side of the IN listen is broken");
                return REINIT_IN_LISTEN;
            }

            /* SHVA reading socket is disconnected */
            if (core->sockets.shva_rw == fd) {
                slog_warn_l("SHVA socket: remote side of the socket (FD = %d) is disconnected", fd);
                return REINIT_SHVA_RW;
            }

            /* OUT writing socket is disconnected */
            if (core->sockets.out_write == fd) {
                slog_warn_l("OUT socket: remote side of the OUT write socket (FD = %d) is disconnected", fd);
                return REINIT_OUT_RW;
            }

            /* OUT listener socket is disconnected */
            if (core->sockets.out_listen == fd) {
                slog_warn_l("OUT socket: remote side of the OUT listen (FD = %d) is disconnected", fd);
                return REINIT_OUT_LISTEN;
            }

            /* Else: it is one of IN reading sockets, we should remove it */
            if (FD_IS_IN == pepa_if_fd_in(core, fd)) {
                pepa_remove_socket_from_epoll(core, fd, "IN", __FILE__, __LINE__);
                pepa_in_reading_sockets_close_rm(core, fd);
                return PEPA_ERR_OK;
            }

            /* Else we got not recognized signal */
            slog_fatal_l("We got a problem with the remote side of a socket that we can not detect: (FD = %d)", fd);
            abort();

        } /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) */

        /*** This side is broken ***/

        if (events & (EPOLLHUP)) {
            /* IN listener socket is degraded */
            if (core->sockets.in_listen == fd) {
                slog_warn_l("IN socket: local side of the IN listen is broken");
                return REINIT_IN_LISTEN;
            }

            /* SHVA reading socket is disconnected */
            if (core->sockets.shva_rw == fd) {
                slog_warn_l("SHVA socket: local side of the socket (FD = %d)  is broken", fd);
                return REINIT_SHVA_RW;
            }

            /* OUT writing socket is disconnected */
            if (core->sockets.out_write == fd) {
                slog_warn_l("OUT socket: local side of the OUT write socket (FD = %d) is broken", fd);
                return REINIT_OUT_RW;
            }

            /* OUT listener socket is disconnected */
            if (core->sockets.out_listen == fd) {
                slog_warn_l("OUT socket: local side of the OUT listen is broken");
                return REINIT_OUT_LISTEN;
            }

            /* Else: it is one of IN reading sockets, we should remove it */
            if (FD_IS_IN == pepa_if_fd_in(core, fd)) {
                pepa_remove_socket_from_epoll(core, fd, "IN", __FILE__, __LINE__);
                pepa_in_reading_sockets_close_rm(core, fd);
                return PEPA_ERR_OK;
            }

            /* Else we got not recognized signal */
            slog_fatal_l("We got a problem with the local side of a socket that we can not detect: (FD = %d)", fd);
            abort();
        } /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) */
    }
    return PEPA_ERR_OK;
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

#if 0 /* SEB */ /* 17/11/2024 */
    rc = set_socket_blocking_mode(new_socket);
    if (rc) {
        slog_fatal_l("Can not set socket into blocking mode");
        abort();
    }
#endif /* SEB */ /* 17/11/2024 */

    if (0 != epoll_ctl_add(core->epoll_fd, new_socket, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add new socket (FD = %d) to epoll set: %s", new_socket, strerror(errno));
        pepa_reading_socket_close(new_socket, "IN FORWARD-READ");
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    /* Add to the array of IN reading sockets */
    pepa_in_reading_sockets_add(core, new_socket);

    slog_warn_l("Added new socket (FD = %d) to epoll set", new_socket);
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
#if 0 /* SEB */ /* 20/11/2024 */
static int pepa_process_fdx_shva(pepa_core_t *core, const struct epoll_event events_array[], const int event_count){
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

        /* First, check that there is a real buffer on the reading socket */
        rc = bytes_available_read(core->sockets.shva_rw);
        if (rc < 0) {
            /* The socket is degraded */
            slog_error_l("The core->sockets.shva_rw is degraded and must be restarted ");
            return REINIT_SHVA_RW;
        }

        if (0 == rc) {
            slog_error_l("The core->sockets.shva_rw does not have bytes to read");
            return REINIT_SHVA_RW;
        }

        rc = pepa_one_direction_copy4(core,
                                      core->buffer, core->internal_buf_size,
                                      //rc = transfer_data4(core,
                                      /* Send to : */core->sockets.out_write, "OUT",
                                      /* From: */ core->sockets.shva_rw, "SHVA",
                                      /*Debug is ON */ 0,
                                      /* RX stat */&core->monitor.shva_rx,
                                      /* TX stat */&core->monitor.out_tx,
                                      /* Max iterations */ /*5*/ 1);

        if (PEPA_ERR_OK == rc) {
            //slog_warn_l("%s: Sent from socket (FD = %d)", "IN-FORWARD", events[i].data.fd);
            return PEPA_ERR_OK;
        }

        slog_note_l("An error on sending buffers from SHVA to OUT: %s", pepa_error_code_to_str(rc));

        /* Something wrong with the socket, should be removed */

        /* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
        if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
            slog_note_l("Could not write to %s; setting system to FAIL", "OUT");
            return REINIT_OUT_RW;
        }

        if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
            /* Here are two cases: the read can be IN or SHVA. IN case of SHVA we must restart all sockets */
            slog_note_l("Could not read from %s; setting system to FAIL", "SHVA");
            return REINIT_SHVA_RW;
        }
    }
    return PEPA_ERR_OK;
}
#endif /* SEB */ /* 20/11/2024 */

static int pepa_process_fdx_shva2(pepa_core_t *core)
{
    int rc = PEPA_ERR_OK;

    /* Test read socket */

    rc = bytes_available_read(core->sockets.shva_rw);
    if (rc < 0) {
        /* The socket is degraded */
        slog_error_l("The core->sockets.shva_rw is degraded and must be restarted ");
        return REINIT_SHVA_RW;
    }

    if (0 == rc) {
        // slog_error_l("The core->sockets.shva_rw does not have bytes to read");
        //return -PEPA_ERR_BAD_SOCKET_READ;
        return PEPA_ERR_OK;
    }

    /* Test write socket */

    rc = bytes_available_write(core->sockets.out_write);
    if (rc < 0) {
        /* The socket is degraded */
        slog_error_l("The OUT socket (FD = %d) is degraded and must be restarted", core->sockets.out_write);
        return REINIT_OUT_RW;
    }

    if (0 == rc) {
        slog_error_l("The OUT socket does not have bytes to write (FD = %d)", core->sockets.out_write);
        return PEPA_ERR_OK;
    }

    rc = pepa_one_direction_copy4(core,
                                  core->buffer,
                                  core->internal_buf_size,
                                  /* Send to : */core->sockets.out_write,
                                  "OUT",
                                  /* From: */ core->sockets.shva_rw,
                                  "SHVA",
                                  /*Debug is ON */ 0,
                                  /* RX stat */&core->monitor.shva_rx,
                                  /* TX stat */&core->monitor.out_tx,
                                  /* Max iterations */ /*5*/ 1);

    if (PEPA_ERR_OK == rc) {
        //slog_warn_l("%s: Sent from socket (FD = %d)", "IN-FORWARD", events[i].data.fd);
        return PEPA_ERR_OK;
    }

    slog_note_l("An error on sending buffers from SHVA to OUT: %s", pepa_error_code_to_str(rc));

    /* Something wrong with the socket, should be removed */

    /* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
    if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
        slog_note_l("Could not write to %s; required REINIT_OUT_RW sockets restart", "OUT");
        return REINIT_OUT_RW;
    }

    if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
        /* Here are two cases: the read can be IN or SHVA. IN case of SHVA we must restart all sockets */
        slog_note_l("Could not write to %s; required REINIT_SHVA_RW sockets restart", "OUT");
        return REINIT_SHVA_RW;
    }

    /* We should never be here */
    slog_fatal_l("We got unknown error when wrote to SHVA: %d", rc);
    abort();
}

static int pepa4_process_fdx2(pepa_core_t *core, const struct epoll_event events_array[], const int event_count)
{
    int32_t rc = PEPA_ERR_OK;
    int32_t i;
    int fd_ignore = -1;

    /* SHVA */

#if 0 /* SEB */ /* 17/11/2024 */
    rc = pepa_process_fdx_shva(core, events_array, event_count);
    if (PEPA_ERR_OK != rc) {
        return rc;
    }
#endif /* SEB */ /* 17/11/2024 */


    for (i = 0; i < event_count; i++) {
        const int fd = events_array[i].data.fd;
        const uint32_t events = events_array[i].events;

        /* Sometime we destroy a socket duting this loop, but still have events related to it */
        if (fd == fd_ignore) {
            continue;
        }

        if (EVENT_NO_BUFFER(events)) {
            continue;
        }

        /* SHVA should receive and forward the buffer */
        if (core->sockets.shva_rw == fd) {
            rc = pepa_process_fdx_shva2(core);
            if (PEPA_ERR_OK != rc) {
                return rc;
            }
            continue;
        }

        /* The IN socket: listening, if there is an event, we should to open a new connection */

        if (core->sockets.in_listen == fd) {
            rc = pepa_in_accept_new_connection(core);
            fd_ignore = fd;

            /* If somethins happened during this process, we stop and return */
            if (PEPA_ERR_OK != rc) {
                return REINIT_IN_LISTEN;
            }
            continue;
        }

        /* Below this point we expect only IN READ sockets */

        if (FD_NOT_IN == pepa_if_fd_in(core, fd)) {
            slog_error_l("We expect here only IN RW sockets, buf it is not: (FD = %d) : %s, event(s) on the fd: %s",
                         fd, pepa_detect_socket_name_by_fd(core, fd),
                         pepa_dump_event(events));

            pepa_in_dump_sockets(core);
            //abort();
            continue;
        }

        /* Read /write from/to socket */

        /* Test that we really have bytes on socket to read from */
        rc = bytes_available_read(fd);
        if (rc < 0) {
            /* The socket is degraded */

            slog_error_l("The IN socket  is degraded and must be restarted ");
            /* Remove the broken IN read socket from the epoll */
            pepa_remove_socket_from_epoll(core, fd, "IN", __FILE__, __LINE__);

            /* Close the broken IN read socket */
            pepa_in_reading_sockets_close_rm(core, fd);

            // return -PEPA_ERR_BAD_SOCKET_READ;
            // return PEPA_ERR_OK;
            continue;
        }

        if (0 == rc) {
            slog_error_l("The IN (FD = %d) socket does not have bytes to read", fd);
            //return -PEPA_ERR_BAD_SOCKET_READ;
            return PEPA_ERR_OK;
        }

        /* Test that we have bytes on the write socket */

        rc = bytes_available_write(core->sockets.shva_rw);
        if (rc < 0) {
            /* The socket is degraded */
            slog_error_l("The SHVA RS socket (FD = %d) is degraded and must be restarted", core->sockets.shva_rw);
            return REINIT_SHVA_RW;
        }

        if (0 == rc) {
            slog_error_l("The SHVA socket (FD = %d) does not have bytes to write", core->sockets.shva_rw);
            return 0;
        }

        rc = pepa_one_direction_copy4(/* 1 */core,
                                      /* 2 */ core->buffer,
                                      /* 3 */ core->internal_buf_size,
                                      /* 4 */ /* Send to : */core->sockets.shva_rw,
                                      /* 5 */ "SHVA",
                                      /* 6 */ /* From: */ fd,
                                      /* 7 */ "IN",
                                      /* 8 */ /*Debug is ON */ 0,
                                      /* 9 */ /* RX stat */(uint64_t *)&core->monitor.in_rx,
                                      /* 10 */ /* TX stat */(uint64_t *)&core->monitor.shva_tx,
                                      /* 11 */ /* Max iterations */ 1);

        if (PEPA_ERR_OK == rc) {
            continue;
        }

        slog_note_l("An error on sending buffers: %s", pepa_error_code_to_str(rc));

        /* Something wrong with the socket, should be removed */

        if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
            slog_note_l("Could not write to %s; setting system to FAIL", "SHVA");
            return REINIT_SHVA_RW;
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

//__attribute__((noreturn))
void pepa4_transfer_loop2(pepa_core_t *core)
{
    int counter = 0;
    int rc;
    struct epoll_event events[EVENTS_NUM];
    struct epoll_event events_copy[EVENTS_NUM];

    pepa4_start(core);

    pepa4_restart_sockets(core, START_START);

    slog_note_l("/// BEGIN: TRANSFER PHASE ///");

    /* Wait until something happened */
    do {
        int32_t event_count = epoll_wait(core->epoll_fd, events, EVENTS_NUM, EPOLL_TIMEOUT);

        /* Interrupted by a signal */
        if (event_count < 0 && EINTR == errno) {
            continue;
        }

        counter++;

        core->monitor.events += event_count;

        /* Copy the evens array, this way they are not changed when we iterate them several times:
           When we iterate events AND read / write the file descriptors, the event array might be reseted, and it is not what we need */
        memcpy(events_copy, events, sizeof(struct epoll_event) * EVENTS_NUM);

        /* Process exceptions */
        rc = pepa_process_exceptions2(core, events_copy, event_count);
        if (rc) {
            pepa4_close_needed_sockets(core, rc);
            //pepa4_close_needed_sockets(core, REINIT_ALL);
            pepa4_restart_sockets(core, START_SOCKETS);
            continue;
        }

        /* Process buffers */
        rc = pepa4_process_fdx2(core, events_copy, event_count);

        /* If there is an error, we must restart all sockets */
        if (PEPA_ERR_OK != rc) {
            pepa4_close_needed_sockets(core, rc);
            // pepa4_close_needed_sockets(core, REINIT_ALL);
            pepa4_restart_sockets(core, START_SOCKETS);
        }

        //if (counter >= 1000) {
        //    return;
        //}

    } while (1);
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Create OUT listening socket
 * @param pepa_core_t* core  Core structure
 * @details This is a blocking function. When it finished,
 *  		the OUT listening socket is created.

 */
static void pepa4_out_open_listening_socket(pepa_core_t *core)
{
    struct sockaddr_in s_addr;

    if (core->sockets.out_listen >= 0) {
        slog_note_l("Trying to open a listening socket while it is already opened (FD = %d)", core->sockets.out_listen);
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
            //slog_warn_l("Can not open listening socket: %s", strerror(errno));
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
static int32_t pepa4_out_wait_connection(__attribute__((unused))
                                         const pepa_core_t *core, const int32_t fd_listen)
{
    struct sockaddr_in s_addr;
    socklen_t addrlen = sizeof(struct sockaddr);
    int32_t fd_read = FD_CLOSED;
    do {
        slog_info_l("Starting accept() waiting");
        fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC);
    } while (fd_read < 0);

    slog_info_l("ACCEPTED CONNECTION: (FD = %d)", fd_read);
    pepa_set_tcp_timeout(fd_read);
    // pepa_set_tcp_send_size(core, fd_read, "OUT");
    slog_note_l("Socket (FD = %d) now it is %s",fd_read, utils_socked_blocking_or_not(fd_read));
    return fd_read;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Wait on OUT listening socket incoming connection
 * @param pepa_core_t* core  Core structure
 * @details This is a blocking function. When it finished,
 *  		the OUT writing socket is valid adn ready.
 */
static void pepa4_accept_out(pepa_core_t *core)
{
    core->sockets.out_write = pepa4_out_wait_connection(core, core->sockets.out_listen);
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Open connection to SHVA server
 * @param pepa_core_t* core  Core structure
 * @details This is a blocking function. When it finished,
 *  		the SHVA socket is opened and valid.
 */
static void pepa4_shva_open_connection(pepa_core_t *core)
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
static void pepa4_in_open_listen_socket(pepa_core_t *core)
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
 * @brief Close all sockets and remove them from the epoll set
 * @param pepa_core_t* core  Core object
 * @return int The next state machine state
 * @details 
 */
#if 1 /* SEB */ /* 18/11/2024 */
int pepa4_close_sockets(pepa_core_t *core){
    int rc;

    /* Remove sockets from the the epoll set */

    pepa_remove_socket_from_epoll(core, core->sockets.in_listen, "IN LISTEN", __FILE__, __LINE__);
    pepa_remove_socket_from_epoll(core, core->sockets.out_listen, "OUT LISTEN", __FILE__, __LINE__);
    pepa_remove_socket_from_epoll(core, core->sockets.out_write, "OUT WRITE", __FILE__, __LINE__);
    pepa_remove_socket_from_epoll(core, core->sockets.shva_rw, "SHVA RW", __FILE__, __LINE__);

    pepa_in_reading_sockets_close_all(core);
    rc = pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN");
    if (rc) {
        slog_warn_l("Could not close socket SHVA (FD = %d)", core->sockets.shva_rw);
    }
    core->sockets.in_listen = FD_CLOSED;

    pepa_reading_socket_close(core->sockets.shva_rw, "SHVA");
    core->sockets.shva_rw = FD_CLOSED;

    pepa_socket_close(core->sockets.out_write, "OUT WRITE");
    core->sockets.out_write = FD_CLOSED;

    rc = pepa_socket_shutdown_and_close(core->sockets.out_listen, "OUT LISTEN");
    if (rc) {
        slog_warn_l("Could not close socket OUT LISTEN (FD = %d)", core->sockets.out_listen);
    }

    core->sockets.out_listen = FD_CLOSED;

    slog_note_l("Finished 'close sockets' phase");
    return PEPA_ERR_OK;
}
#endif /* SEB */ /* 18/11/2024 */

static int pepa4_start_out_listen(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: OUT LISTEN PHASE ///");

    /* Looks like the listening socket is opened? */
    if (FD_CLOSED != core->sockets.out_listen) {
        if (YES == pepa_util_is_socket_valid(core->sockets.out_listen)) {
            slog_note_l("/// FINISH: OUT LISTEN PHASE (Socket is OK) ///");
            return 0;
        }

        if (NO == pepa_util_is_socket_valid(core->sockets.out_listen)) {
            pepa_remove_socket_from_epoll(core, core->sockets.out_listen, "OUT WRITE", __FILE__, __LINE__);
            pepa_reading_socket_close(core->sockets.out_listen, "OUT WRITE");
            core->sockets.out_listen = FD_CLOSED;

            slog_note_l("The socket core->sockets.out_listen (FD = %d)) should be valid but it is invalid", core->sockets.out_listen);
        }
    }

    /* This function is blocking and when it returned, the OUT listening socket is opened */
    pepa4_out_open_listening_socket(core);

    /* Add to epoll set */
    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.out_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add OUT Listen socket (FD = %d) to epoll set: %s", core->sockets.out_listen, strerror(errno));
        slog_error_l("/// ERROR: START OUT LISTEN PHASE ///");
        return -1;
    }

    slog_note_l("/// FINISH:  OUT LISTEN PHASE ///");
    return 0;
}

static int pepa4_start_out_rw(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: START OUT RW PHASE ///");

    /* Looks like the listening socket is opened? */
    if (FD_CLOSED != core->sockets.out_write) {
        if (YES == pepa_util_is_socket_valid(core->sockets.out_write)) {
            slog_note_l("/// FINISH: OUT RW PHASE (socket is OK) (FD = %d) ///", core->sockets.out_write);
            return 0;
        }

        /* No, the socket is invalid */
        slog_note_l("The socket core->sockets.out_write (FD = %d) should be valid but it is invalid", core->sockets.out_write);
        pepa_disconnect_out_rw(core);
    }

    pepa4_accept_out(core);

    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add OUT Write (FD = %d) socket to epoll set: %s", core->sockets.out_write, strerror(errno));
        slog_error_l("/// ERROR: OUT RW PHASE ///");
        return -1;
    }

    slog_note_l("opened OUT socket: LISTEN: (FD = %d), WRITE: (FD = %d)", core->sockets.out_listen, core->sockets.out_write);
    slog_note_l("/// FINISH: OUT RW PHASE ///");
    return 0;
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

    slog_info_l("ACCEPTED [%s] CONNECTION: (FD = %d)", name, fd_read);
    pepa_set_tcp_timeout(fd_read);
    pepa_set_tcp_send_size(core, fd_read, name);
    return fd_read;
}

static void pepa4_start_in_rw(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: START IN RW PHASE ///");
    slog_info_l(">>>>> Starting adding %ld IN Read sockets to epoll set", core->readers_preopen);

    do {
        int32_t fd_read = pepa_wait_connection(core, core->sockets.in_listen, "IN");
        pepa_in_reading_sockets_add(core, fd_read);

        if (0 != epoll_ctl_add(core->epoll_fd, fd_read, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            slog_error_l("Can not add IN Read socket (FD = %d) to epoll set: %s", fd_read, strerror(errno));
            pepa_in_reading_sockets_close_rm(core, fd_read);
        }
        slog_info_l("Added IN Read socket (FD = %d) to epoll set", fd_read);
    } while (pepa_in_num_active_sockets(core) < (int)core->readers_preopen);

    slog_note_l(">>>>> Finished adding %ld IN Read sockets to epoll set", core->readers_preopen);
    slog_note_l("/// FINISH: START IN RW PHASE ///");
}

static int pepa4_start_shva(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: START SHVA PHASE ///");
    if (FD_CLOSED != core->sockets.shva_rw) {
        slog_note_l("/// FINISH: START SHVA PHASE: the fd is already inited ///");
        return 0;
    }
    /* This is an blocking function, returns only when SHVA is opened */
    pepa4_shva_open_connection(core);
    slog_note_l("Opened shva socket: Blocking? %s", utils_socked_blocking_or_not(core->sockets.shva_rw));
    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.shva_rw, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add SHVA socket (FD = %d) to epoll set: %s", core->sockets.shva_rw, strerror(errno));
        slog_error_l("/// ERROR: START SHVA PHASE ///");
        return -1;
    }
    slog_note_l("Added SHVA RW socket (FD = %d) to epoll: Blocking? %s", core->sockets.shva_rw, utils_socked_blocking_or_not(core->sockets.shva_rw));

    slog_note_l("Opened SHVA socket: (FD = %d)", core->sockets.shva_rw);
    slog_note_l("/// FINISH: START SHVA PHASE ///");
    return 0;
}

static int pepa4_start_in_listen(pepa_core_t *core)
{
    slog_note_l("/// BEGIN: START IN LISTEN PHASE ///");
    /* This is a blocking function */
    if (core->sockets.in_listen >= 0) {
        slog_note_l("/// FINISH: START IN LISTEN (in_listen > 0) ///");
        return 0;
    }

    pepa4_in_open_listen_socket(core);

    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.in_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("Can not add IN Listen socket (FD = %d) to epoll set: %s", core->sockets.in_listen, strerror(errno));
        slog_error_l("/// ERROR: START IN LISTEN ///");
        return -1;
    }
    slog_note_l("Opened IN LISTEN: (FD = %d)", core->sockets.in_listen);
    slog_note_l("/// FINISH: START IN LISTEN ///");
    return 0;
}

int pepa4_restart_sockets(pepa_core_t *core, int next_state)
{
    int rc;

    slog_note_l("Strating sockets restart from %d", next_state);
    slog_note_l("===============================================================================================");
    slog_note(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    TESTP(core, -1);
    if (NO == pepa_core_is_valid(core)) {
        slog_error_l("Core structure is invalid");
        return -1;
    }

    // int next_state = start_from;

    do {
        switch (next_state) {
            case START_START:
                //pepa4_start(core);
                next_state = START_OUT_LISTEN;
                break;

            case START_OUT_LISTEN:
                rc = pepa4_start_out_listen(core);
                if (rc) {
                    slog_fatal_l("Unrecovable error on START_OUT_LISTEN");
                    abort();
                }
                next_state = START_IN_LISTEN;
                break;

            case START_IN_LISTEN:
                rc = pepa4_start_in_listen(core);
                if (rc) {
                    slog_fatal_l("Unrecovable error on START_IN_LISTEN");
                    abort();
                }
                next_state = START_OUT_RW;
                break;

            case START_OUT_RW:
                rc = pepa4_start_out_rw(core);
                if (rc) {
                    slog_fatal_l("Unrecovable error on START_OUT_RW");
                    abort();
                }
                next_state = START_IN_RW;
                break;

            case START_IN_RW:
                pepa4_start_in_rw(core);
                next_state = START_SHVA;
                break;

            case START_SHVA:
                rc = pepa4_start_shva(core);
                if (rc) {
                    slog_fatal_l("Unrecovable error on START_SHVA");
                    abort();
                }
                next_state = START_DONE;
                break;
            case START_DONE:
                // slog_note_l("===============================================================================================");
                slog_note("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
                slog_note_l("Socket restart done OK");
                return 0;

            default:
                slog_fatal_l("You should never be here");
                abort();
        }
    } while (1);
    //slog_note_l("===============================================================================================");
    slog_note_l("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
    return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (16/11/2024)
 * @brief This function prepares sockets restart in case one of sockets degraded
 * @param int what  Whic socket degraded?
 * @details 
 */
void pepa4_close_needed_sockets(pepa_core_t *core, const int what)
{
    slog_note_l("Start sockets reset");
    //slog_note_l("===============================================================================================");
    slog_note(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    switch (what) {
        case REINIT_SHVA_RW:
            slog_note_l("Reset is: REINIT_SHVA_RW");
            /* Kill all IN RW sockets  */
            pepa_disconnect_in_rw(core);
            pepa_disconnect_shva(core);

            break;
        case REINIT_OUT_RW:
            slog_note_l("Reset is: REINIT_OUT_RW");
            pepa_disconnect_in_rw(core);
            pepa_disconnect_shva(core);
            pepa_disconnect_out_rw(core);
            break;
        case REINIT_OUT_LISTEN:
            slog_note_l("Reset is: REINIT_OUT_LISTEN");
            pepa_disconnect_out_listen(core);
            pepa_disconnect_in_rw(core);
            pepa_disconnect_shva(core);
            pepa_disconnect_out_rw(core);

            break;
        case REINIT_IN_RW:
            slog_note_l("Reset is: REINIT_IN_RW");
            /* Do nothing */
            break;
        case REINIT_IN_LISTEN:
            slog_note_l("Reset is: REINIT_IN_LISTEN");
            /* Kill all IN RW */
            /* Kill SHVA */
            pepa_disconnect_shva(core);
            pepa_disconnect_in_rw(core);
            pepa_disconnect_in_listen(core);
            break;
        default:
            slog_fatal_l("Unknown reinit request: %d; stop", what);
            abort();
    }
    // slog_note_l("===============================================================================================");
    slog_note("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
    slog_note_l("End sockets reset");
}

