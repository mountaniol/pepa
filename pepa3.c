#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "logger.h"
#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_state_machine.h"
#include "pepa3.h"
#include "pepa_in_reading_sockets.h"

/**
 * @author Sebastian Mountaniol (1/9/24)
 * @brief PEPA state machine states
 * @details 
 */
enum pepa3_go_states {
    PST_START = 1000, /**< Start state, executed  once  */
    PST_CLOSE_SOCKETS, /**< All sockets must be closed */
    PST_RESET_SOCKETS, /**< Only reading sockets should be closed, listeners stay */
    PST_WAIT_OUT, /**< Wit OUT connection */
    PST_OPEN_SHVA, /**< Connect to SHVA server */
    PST_START_IN, /**< Start IN listening socket */
    PST_WAIT_IN, /**< Wait until several IN socket are connected */
    PST_RESTART_IN, /**< IN listening socket reuires full restart */
    PST_TRANSFER_LOOP, /**< Start transfering loop */
    PST_END, /**< PEPA must exit */
};

#if 0 /* SEB */
enum pepa3_transfer_states {
    TR_IN_CONNECTION = 2000,
    TR_SHVA_READ,
    TR_SHVA_IN_READ,
};
#endif

/**
 * @author Sebastian Mountaniol (1/9/24)
 * @brief Internal statuses 
 * @details 
 */
enum pepa3_errors {
    TE_RESTART = 3000, /**< All sockets need full restart */
    TE_RESET, /**< Sockets need partial restart, listening sockets are OK */
    TE_IN_RESTART, /**< IN sockets need full restart */
    TE_IN_REMOVED, /**< On of IN listening sockets was removed */
};

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
static int pepa_process_exceptions(pepa_core_t *core, const struct epoll_event events[], const int event_count)
{
    int ret       = PEPA_ERR_OK;
    int rc_remove;
    int i;
    for (i = 0; i < event_count; i++) {
        if (!(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
            //if (!(events[i].events & (EPOLLRDHUP | EPOLLHUP))) {
            continue;
        }

        /*** The remote side is disconnected ***/

        /* If one of the read/write sockets is diconnected, stop */
        if (events[i].events & (EPOLLRDHUP)) {

            /* SHVA reading socket is disconnected */
            if (core->sockets.shva_rw == events[i].data.fd) {
                //llog_w("SHVA socket: remote side of the socket is disconnected");
                llog_w("SHVA socket: remote side of the socket is disconnected");
                return TE_RESTART;
            }

            /* OUT writing socket is disconnected */
            if (core->sockets.out_write == events[i].data.fd) {
                // llog_w("OUT socket: remote side of the OUT write socket is disconnected");
                llog_w("OUT socket: remote side of the OUT write socket is disconnected");
                return TE_RESTART;
            }

            /* OUT listener socket is disconnected */
            if (core->sockets.out_listen == events[i].data.fd) {
                llog_w("OUT socket: remote side of the OUT listen is disconnected");
                return TE_RESTART;
            }

            /* Else: it is one of IN reading sockets, we should remove it */
            rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
            if (rc_remove) {
                llog_w("Could not remove IN reading socket %d from epoll set", events[i].data.fd);
            } else {
                llog_w("Removed IN reading socket %d from epoll set", events[i].data.fd);
            }
            pepa_in_reading_sockets_close_rm(core, events[i].data.fd);
            ret = TE_IN_REMOVED;

        } /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) */

        /*** This side is broken ***/

        if (events[i].events & (EPOLLHUP)) {

            /* SHVA reading socket is disconnected */
            if (core->sockets.shva_rw == events[i].data.fd) {
                llog_w("SHVA socket: local side of the socket is broken");
                return TE_RESTART;
            }

            /* OUT writing socket is disconnected */
            if (core->sockets.out_write == events[i].data.fd) {
                llog_w("OUT socket: local side of the OUT write socket is broken");
                return TE_RESTART;
            }

            /* OUT listener socket is disconnected */
            if (core->sockets.out_listen == events[i].data.fd) {
                llog_w("OUT socket: local side of the OUT listen is broken");
                return TE_RESTART;
            }

            /* IN listener socket is degraded */
            if (core->sockets.in_listen == events[i].data.fd) {
                llog_w("IN socket: local side of the IN listen is broken");
                return TE_IN_RESTART;
            }

            /* Else: it is one of IN reading sockets, we should remove it */
            rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
            if (rc_remove) {
                llog_w("Could not remove socket %d from epoll set", events[i].data.fd);
            }
            pepa_in_reading_sockets_close_rm(core, events[i].data.fd);
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
    int32_t            new_socket = FD_CLOSED;
    static int32_t     addrlen    = sizeof(address);

    if ((new_socket = accept4(core->sockets.in_listen,
                              (struct sockaddr *)&address,
                              (socklen_t *)&addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC)) < 0) {
        llog_e("Error on accept: %s", strerror(errno));
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    // pepa_set_tcp_connection_props(core, new_socket);
    pepa_set_tcp_timeout(new_socket);
    pepa_set_tcp_recv_size(core, new_socket);
    const int enable = 1;
    int       rc     = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (rc < 0) {
        llog_e("Open Socket: Could not set SO_REUSEADDR on socket, error: %s", strerror(errno));
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    if (0 != epoll_ctl_add(core->epoll_fd, new_socket, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        llog_e("Can not add new socket to epoll set: %s", strerror(errno));
        pepa_reading_socket_close(new_socket, "IN FORWARD-READ");
        return (-PEPA_ERR_SOCKET_CREATION);
    }

    /* Add to the array of IN reading sockets */
    pepa_in_reading_sockets_add(core, new_socket);

    llog_i("Added new socket %d to epoll set", new_socket);
    return PEPA_ERR_OK;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Process only SHVA buffers incoming buffers
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
static int pepa_process_fdx_shva(pepa_core_t *core, const struct epoll_event events[], const int event_count)
{
    int32_t rc = PEPA_ERR_OK;
    int32_t i;

    for (i = 0; i < event_count; i++) {

        if (!(events[i].events & EPOLLIN)) {
            continue;
        }

        if (core->sockets.shva_rw != events[i].data.fd) {
            continue;
        }

        /* Read /write from/to socket */

        rc = pepa_one_direction_copy3(core,
                                      /* Send to : */core->sockets.out_write, "OUT",
                                      /* From: */ core->sockets.shva_rw, "SHVA",
                                      core->buffer, core->internal_buf_size,
                                      /* Debug is ON */ 0,
                                      /* Add pepa ID and/or Ticket; 0 = no */ 1,
                                      /* RX stat */&core->monitor.shva_rx,
                                      /* TX stat */&core->monitor.out_tx,
                                      /* Max iterations */ 1);

        if (PEPA_ERR_OK == rc) {
            //llog_w("%s: Sent from socket %d", "IN-FORWARD", events[i].data.fd);
            return PEPA_ERR_OK;
        }

        llog_n("An error on sending buffers from SHVA to OUT: %s", pepa_error_code_to_str(rc));

        /* Something wrong with the socket, should be removed */

        /* Writing side is off, means: SHVA or OUT socket is invalid */
        /* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
        if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
            llog_n("Could not write to %s; setting system to FAIL", "OUT");
        }

        if (-PEPA_ERR_BAD_SOCKET_READ == rc) {
            /* Here are two cases: the read can be IN or SHVA. IN case of SHVA we must restart all sockets */
            llog_n("Could not read from %s; setting system to FAIL", "SHVA");
        }

        return TE_RESTART;
    }
    return PEPA_ERR_OK;
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
static int pepa_process_fdx(pepa_core_t *core, const struct epoll_event events[], const int event_count)
{
    int32_t rc = PEPA_ERR_OK;
    int32_t i;

    for (i = 0; i < event_count; i++) {
        if (!(events[i].events & EPOLLIN)) {
            continue;
        }

        /* The IN socket: listening, if there is an event, we should to open a new connection */
        if (core->sockets.in_listen == events[i].data.fd) {
            rc = pepa_in_accept_new_connection(core);

            /* If somethins happened during this process, we stop and return */
            if (PEPA_ERR_OK != rc) {
                return rc;
            }
            continue;
        }

        /* Read /write from/to socket */

        rc = pepa_one_direction_copy3(core,
                                      /* Send to : */ core->sockets.shva_rw, "SHVA",
                                      /* From: */ events[i].data.fd, "IN",
                                      core->buffer, core->internal_buf_size,
                                      /*Debug is ON */ 0,
                                      /* Add pepa ID and/or Ticket; 0 = no */ 0,
                                      /* RX stat */&core->monitor.in_rx,
                                      /* TX stat */&core->monitor.shva_tx,
                                      /* Max iterations */ 1);

        if (PEPA_ERR_OK == rc) {
            //llog_w("%s: Sent from socket %d", "IN-FORWARD", events[i].data.fd);
            continue;
        }

        llog_w("An error on sending buffers: %s", pepa_error_code_to_str(rc));

        /* Something wrong with the socket, should be removed */

        /* Writing side is off, means: SHVA or OUT socket is invalid */
        /* Write socket is always SHVA or OUT; if there is an error ont write, we must restare the system */
        if (-PEPA_ERR_BAD_SOCKET_WRITE == rc) {
            llog_n("Could not write to %s; setting system to FAIL", "SHVA");
            return TE_RESTART;
        }

        if (-PEPA_ERR_BAD_SOCKET_READ == rc) {


            /* If it is not SHVA, it is one of IN sockets; just close and remove from the set */

            llog_n("Could read from %s; removing the IN fd %d", "IN", events[i].data.fd);
            /* Remove the broken IN read socket from the epoll */
            int rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);

            if (rc_remove) {
                llog_w("Could not remove socket %d from epoll set", events[i].data.fd);
            }

            /* Close the broken IN read socket */
            pepa_in_reading_sockets_close_rm(core, events[i].data.fd);
        }
    }

    rc = pepa_process_fdx_shva(core, events, event_count);
    if (PEPA_ERR_OK != rc) {
        return rc;
    }

    return PEPA_ERR_OK;
}

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
static int         pepa3_transfer_loop(pepa_core_t *core)
{
    int                next_state         = PST_TRANSFER_LOOP;
    int                rc;
    struct epoll_event events[EVENTS_NUM];
    llog_n("Starting 'trensfer loop' phase");
    /* Wait until something happened */
    do {
        int32_t event_count = epoll_wait(core->epoll_fd, events, EVENTS_NUM, EPOLL_TIMEOUT);

        /* Interrupted by a signal */
        if (event_count < 0 && EINTR == errno) {
            continue;
        }

        /* Process exceptions */
        rc = pepa_process_exceptions(core, events, event_count);

        switch (rc) {
        case PEPA_ERR_OK:
            break;
        case TE_RESTART:
            /* An error on SHVA or OUT listening sockets occured, all sockets should be restarted */
            next_state = PST_CLOSE_SOCKETS;
            break;
        case TE_IN_RESTART:
            /* An error on listening IN socket occured, the IN socket should be restarted */
            next_state = PST_RESTART_IN;
            break;
        case TE_IN_REMOVED:
            /* Do nothing, this is a normal situation, one of IN readers was removed */
            /* However do not process file descriptors on this iteration;
               start a new iteration and pn the new iteration the removed socket will not appear in the set */
            //continue;
            break;
        default:
            llog_e("You should never be here: got status %d", rc);
            abort();
        }

        /* If there is an error, we must restart all sockets */
        if (TE_RESTART == rc) {
            return PST_CLOSE_SOCKETS;
        }

        /* Process buffers */
        rc = pepa_process_fdx(core, events, event_count);

        /* If there is an error, we must restart all sockets */
        if (PEPA_ERR_OK != rc) {
            return PST_CLOSE_SOCKETS;
        }
    } while (PST_TRANSFER_LOOP == next_state);

    llog_n("Finished 'transfet loop' phase");
    return next_state;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Create OUT listening socket
 * @param pepa_core_t* core  Core structure
 * @details This is a blocking function. When it finished,
 *  		the OUT listening socket is created.

 */
static void        pepa_out_open_listening_socket(pepa_core_t *core)
{
    struct sockaddr_in s_addr;

    if (core->sockets.out_listen >= 0) {
        llog_n("Trying to open a listening socket while it is already opened");
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
            //llog_w("Can not open listening socket: %s", strerror(errno));
            usleep(1000000);
        }
    } while (core->sockets.out_listen < 0);
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief This function implements IN writing socket accept.
 *  	  THis function will be locked until an external actor is connected
 * @param const pepa_core_t* core     Core structure
 * @param const int32_t fd_listen Listening OUT socket
 * @return int32_t Socket file descriptor of the writing OUT
 *  	   socket
 */
static int32_t     pepa_wait_connection(const pepa_core_t *core, const int32_t fd_listen, const char *name)
{
    struct sockaddr_in s_addr;
    socklen_t          addrlen = sizeof(struct sockaddr);
    int32_t            fd_read = FD_CLOSED;
    do {
        llog_i("Starting accept() waiting");
        fd_read = accept4(fd_listen, &s_addr, &addrlen, SOCK_CLOEXEC | SOCK_NONBLOCK);
    } while (fd_read < 0);

    llog_i("ACCEPTED [%s] CONNECTION: fd = %d", name, fd_read);
    pepa_set_tcp_timeout(fd_read);
    pepa_set_tcp_send_size(core, fd_read);
    return fd_read;
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Wait on OUT listening socket incoming connection
 * @param pepa_core_t* core  Core structure
 * @details This is a blocking function. When it finished,
 *  		the OUT writing socket is valid adn ready.
 */
static void pepa_accept_out_connections(pepa_core_t *core)
{
    int32_t     fd_read                = pepa_wait_connection(core, core->sockets.out_listen, "OUT");
    core->sockets.out_write = fd_read;
}

static void pepa_accept_in_connections(pepa_core_t *core)
{
    do {
        int32_t     fd_read                = pepa_wait_connection(core, core->sockets.in_listen, "IN");
        pepa_in_reading_sockets_add(core, fd_read);
    } while (core->in_reading_sockets.active < (size_t)core->readers_preopen);
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

    llog_n("Opened connection to SHVA");
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Create IN listening socket
 * @param pepa_core_t* core  Core strucutre
 * @details This is a blocking function. When it finished,
 *  		the IN listening socket is opened.
 */
static void        pepa_in_open_listen_socket(pepa_core_t *core)
{
    struct sockaddr_in s_addr;
    while (1) {
        /* Just try to close it */
        pepa_socket_close_in_listen(core);
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
    core->buffer = calloc(core->internal_buf_size, 1);

    if (NULL == core->buffer) {
        llog_e("Can not allocate a transfering buffer, stopped");
        exit(1);
    }
    core->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (core->epoll_fd < 0) {
        llog_e("Can not create eventfd file descriptor, stopped");
        free(core->buffer);
        exit(2);
    }

    /* TODO: Instead of 1024 make it configurable */
    pepa_in_reading_sockets_allocate(core, PEPA_IN_SOCKETS);

    llog_n("Finished 'start' phase");
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
    /* IN listening socket: remove from the epoll and close */
    int rc                  = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.in_listen, NULL);
    if (rc) {
        llog_w("Could not remove socket IN Listen from epoll set: fd: %d, %s", core->sockets.in_listen, strerror(errno));
    }

    rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.out_listen, NULL);
    if (rc) {
        llog_w("Could not remove socket OUT Listen from epoll set: fd: %d, %s", core->sockets.out_listen, strerror(errno));
    }

    rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);
    if (rc) {
        llog_w("Could not remove socket OUT Write from epoll set: %d:, %s", core->sockets.out_write, strerror(errno));
    }

    rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.shva_rw, NULL);
    if (rc) {
        llog_w("Could not remove socket SHVA from epoll set: fd: %d, %s", core->sockets.shva_rw, strerror(errno));
    }

    pepa_in_reading_sockets_close_all(core);
    rc = pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN");
    if (rc) {
        llog_w("Could not close socket SHVA: fd: %d", core->sockets.shva_rw);
    }
    core->sockets.in_listen = FD_CLOSED;

    pepa_reading_socket_close(core->sockets.shva_rw, "SHVA");
    core->sockets.shva_rw = FD_CLOSED;

    pepa_socket_close(core->sockets.out_write, "OUT WRITE");
    core->sockets.out_write = FD_CLOSED;

    rc = pepa_socket_shutdown_and_close(core->sockets.out_listen, "OUT LISTEN");
    if (rc) {
        llog_w("Could not close socket OUT LISTEN: fd: %d", core->sockets.out_listen);
    }

    core->sockets.out_listen = FD_CLOSED;

    llog_n("Finished 'close sockets' phase");
    return PST_WAIT_OUT;
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
static int pepa3_reset_sockets(pepa_core_t *core)
{
    int        rc;
    llog_n("Starting socket restart");
#if 0 /* SEB */
    int rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.out_listen, NULL);
    if (rc) {
        llog_w("Could not remove socket OUT Listen from epoll set: fd: %d, %s", core->sockets.out_listen, strerror(errno));
    }
#endif

    rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);
    if (rc) {
        llog_w("Could not remove socket OUT Write from epoll set: %d, %s", core->sockets.out_write, strerror(errno));
    }

    rc = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.shva_rw, NULL);
    if (rc) {
        llog_w("Could not remove socket SHVA from epoll set: fd: %d, %s", core->sockets.shva_rw, strerror(errno));
    }

    llog_n("Removed out_write and shwa_rw from epoll");

    // pepa_in_reading_sockets_close_all(core);

    pepa_reading_socket_close(core->sockets.shva_rw, "SHVA");
    core->sockets.shva_rw = FD_CLOSED;

    llog_n("Closed shva_rw");

    pepa_socket_close(core->sockets.out_write, "OUT WRITE");
    core->sockets.out_write = FD_CLOSED;

    llog_n("Closed out_write");

    /* CLose all IN reading sockets */
    pepa_in_reading_sockets_close_all(core);

    llog_n("Closed all IN readers");

#if 0 /* SEB */
    rc = pepa_socket_shutdown_and_close(core->sockets.out_listen, "OUT LISTEN");
    if (rc) {
        llog_w("Could not close socket OUT LISTEN: fd: %d", core->sockets.out_listen);
    }

    core->sockets.out_listen = FD_CLOSED;
#endif

    llog_n("Finished 'close sockets' phase");
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
    /* Both these functions are blocking and when they returned, both OUT sockets are opened */
    llog_n("Startng 'wait out' phase");
    if (core->sockets.out_listen < 0) {
        pepa_out_open_listening_socket(core);
        if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.out_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            llog_e("Can not add OUT Listen socket to epoll set: %s", strerror(errno));
            return PST_CLOSE_SOCKETS;
        }
    }

    pepa_accept_out_connections(core);

    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        llog_e("Can not add OUT Write socket to epoll set: %s", strerror(errno));
        return PST_CLOSE_SOCKETS;
    }
    llog_n("Finished 'wait out' phase");
    return PST_START_IN;
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
    llog_n("Starting 'wait IN' phase");
    /* Both these functions are blocking and when they returned, both OUT sockets are opened */
    if (core->sockets.in_listen < 0) {
        llog_e("IN Listening socket is closed, it must be opened");
        return PST_START_IN;
    }

    pepa_accept_in_connections(core);

    /* Add all accepted IN connection to the epoll */

    for (int idx = 0; idx < core->in_reading_sockets.number; idx++) {
        if (EMPTY_SLOT == core->in_reading_sockets.sockets[idx]) {
            continue;
        }

        if (0 != epoll_ctl_add(core->epoll_fd, core->in_reading_sockets.sockets[idx], EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            llog_e("Can not add IN Read socket to epoll set: %s", strerror(errno));
            return PST_CLOSE_SOCKETS;
        }
    }

    llog_n("Finished 'wait in' phase");
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
    llog_n("Starting 'open SHVA' phase");
    /* This is an blocking function, returns only when SHVA is opened */
    pepa_shva_open_connection(core);
    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.shva_rw, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        llog_e("Can not add SHVA socket to epoll set: %s", strerror(errno));
        return PST_CLOSE_SOCKETS;
    }

    llog_n("Finished 'open SHVA' phase");
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
    llog_n("Starting 'start IN' phase");
    /* This is a blocking function */
    if (core->sockets.in_listen >= 0) return PST_WAIT_IN;

    pepa_in_open_listen_socket(core);

    if (0 != epoll_ctl_add(core->epoll_fd, core->sockets.in_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        llog_e("Can not add IN Listening socket to epoll set: %s", strerror(errno));
        return PST_CLOSE_SOCKETS;
    }
    llog_n("Finished 'start IN' phase");
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
    llog_n("Starting 'restart IN' phase");
    int        rc               = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->sockets.in_listen, NULL);
    if (rc) {
        llog_w("Could not remove socket IN Listen from epoll set: fd: %d, %s", core->sockets.in_listen, strerror(errno));
    }

    pepa_in_reading_sockets_close_all(core);
    rc = pepa_socket_shutdown_and_close(core->sockets.in_listen, "IN LISTEN");
    if (rc) {
        llog_w("Could not close listening IN socket: fd: %d", core->sockets.shva_rw);
    }
    core->sockets.in_listen = FD_CLOSED;
    llog_n("Finished 'restart IN' phase");
    return pepa3_start_in(core);
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
    llog_n("Starting 'END' phase");
    pepa3_close_sockets(core);
    pepa_in_reading_sockets_free(core);
    close(core->epoll_fd);
    llog_n("Finished 'END' phase");
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
    if (!pepa_core_is_valid(core)) {
        llog_e("Core structure is invalid");
        return -1;
    }
    int next_state = PST_START;

    do {
        switch (next_state) {
        case PST_START:
            next_state = pepa3_start(core);
            break;
        case PST_CLOSE_SOCKETS:
            //next_state = pepa3_close_sockets(core);
            next_state = pepa3_reset_sockets(core);
            break;
        case PST_WAIT_OUT:
            next_state = pepa3_wait_out(core);
            break;
        case PST_START_IN:
            next_state = pepa3_start_in(core);
            break;
        case PST_WAIT_IN:
            next_state = pepa3_wait_in(core);
            break;
        case PST_OPEN_SHVA:
            next_state = pepa3_open_shva(core);
            break;
        case PST_RESTART_IN:
            next_state = pepa3_restart_in(core);
            break;
        case PST_TRANSFER_LOOP:
            next_state = pepa3_transfer_loop(core);
            break;
        case PST_END:
            return pepa3_end(core);
        default:
            llog_e("You should never be here");
            abort();
        }
    } while (1);

    return PEPA_ERR_OK;
}

#if 0
int pepa_go_prev(pepa_core_t *core) {
    TESTP(core, -1);
    if (!pepa_core_is_valid(core)) {
        llog_e("Core structure is invalid");
        return -1;
    }
    int next_state = PST_START;

    do {
        switch (next_state) {
            case PST_START:
            next_state = pepa3_start(core);
            break;
            case PST_CLOSE_SOCKETS:
            //next_state = pepa3_close_sockets(core);
            next_state = pepa3_reset_sockets(core);
            break;
            case PST_WAIT_OUT:
            next_state = pepa3_wait_out(core);
            break;
            case PST_OPEN_SHVA:
            next_state = pepa3_open_shva(core);
            break;
            case PST_START_IN:
            next_state = pepa3_start_in(core);
            break;
            case PST_RESTART_IN:
            next_state = pepa3_restart_in(core);
            break;
            case PST_TRANSFER_LOOP:
            next_state = pepa3_transfer_loop(core);
            break;
            case PST_END:
            return pepa3_end(core);
            default:
            llog_e("You should never be here");
            abort();
        }
    } while (1);

    return PEPA_ERR_OK;
}
#endif
