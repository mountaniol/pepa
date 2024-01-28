#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_state_machine.h"
#include "pepa_in_reading_sockets.h"

/* Used internally in IN reading sockets array */
#define EMPTY_SLOT (-1)

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Close all IN reading sockets and remove them from
 *  	  epoll set
 * @param pepa_core_t* core  Core object
 * @details 
 */
void pepa_in_reading_sockets_close_all(pepa_core_t *core)
{
	int i;

	if(NULL == core) {
		slog_error_l("Core is NULL");
		return;
	}

	if (0 == pepa_core_is_valid(core)) {
		slog_error_l("Core structure is invalid");
		return;
	}

	if(NULL == core->in_reading_sockets.sockets) {
		slog_error_l("core->in_reading_sockets.sockets is NULL");
		return;
	}

	slog_note_l("IN-READER: Starting closing and removing sockets: %d slots", core->in_reading_sockets.number);

	for (i = 0; i < core->in_reading_sockets.number; i++) {
		if (EMPTY_SLOT != core->in_reading_sockets.sockets[i]) {

			//close(core->in_reading_sockets.sockets[i]);
			slog_note_l("IN-READER: Going to close in reading socket %d port %d",
						core->in_reading_sockets.sockets[i],
						pepa_find_socket_port(core->in_reading_sockets.sockets[i]));

			int rc_remove = epoll_ctl(core->epoll_fd, EPOLL_CTL_DEL, core->in_reading_sockets.sockets[i], NULL);

			if (rc_remove) {
				slog_warn_l("IN-READER: Could not remove socket %d from epoll set", core->in_reading_sockets.sockets[i]);
			}

			pepa_reading_socket_close(core->in_reading_sockets.sockets[i], "IN FORWARD READ");
			slog_note_l("IN-READER: Closed socket %d in slot %d", core->in_reading_sockets.sockets[i], i);
			core->in_reading_sockets.sockets[i] = EMPTY_SLOT;
		}
	}
	slog_note_l("IN-READER: Finished closing and removing sockets: %d slots", core->in_reading_sockets.number);
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Release array of IN reading sockets
 * @param pepa_core_t* core  Core structure
 * @details This function deallocates an array of IN reading socket
 *          file descriptors. Before it is deallocated, all active
 *          file descriptors are removed and closed. So the user
 *          should not remove them and close them. This function
 *          makes it all. It will call
 *          pepa_in_reading_sockets_close_all() which also
 *          removes the reading socket file descriptors from
 *          epoll set.
 */
void pepa_in_reading_sockets_free(pepa_core_t *core)
{
	slog_note_l("IN-READER: Starting IN read sockets closing and cleaning");
	TESTP_VOID(core);
	if (!pepa_core_is_valid(core)) {
		slog_error_l("Core structure is invalid");
		return;
	}
	TESTP_VOID(core->in_reading_sockets.sockets);

	pepa_in_reading_sockets_close_all(core);
	free(core->in_reading_sockets.sockets);
	core->in_reading_sockets.sockets = NULL;
	slog_note_l("IN-READER: Finished IN reading socket closing and cleaning");
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Allocate array to keep of IN reading sockets
 * @param pepa_core_t* core  Core structure
 * @param const int num   Number of slots in the array
 * @details This function allocate an array of 'num' elements to
 *  		keep IN reading sockets
 */
void pepa_in_reading_sockets_allocate(pepa_core_t *core, const int num)
{
	int i;
	slog_note_l("IN-READER: Starting IN read sockets array allocation");
	TESTP_VOID(core);

	if (0 == pepa_core_is_valid(core)) {
		slog_error_l("Core structure is invalid");
		return;
	}
	
	core->in_reading_sockets.number = num;
	core->in_reading_sockets.sockets = (int *)malloc(sizeof(int) * (size_t)num);
	for (i = 0; i < num; i++) {
		core->in_reading_sockets.sockets[i] = EMPTY_SLOT;
	}
	slog_note_l("IN-READER: Finished IN read sockets array allocation: allocated %d socket slots", num);
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Add an IN reading socket into the array of opened sockets
 * @param pepa_core_t* core  Core structure
 * @param const int fd   IN Reading socket
 * @details When a new IN reader is connected to
 *              PEPA, the reading socket file descriptor, is
 *              added to the internal array of IN reading
 *              sockets. NOTE: This function does not add the
 *              socket into epoll set
 */
void pepa_in_reading_sockets_add(pepa_core_t *core, const int fd)
{
	int i;
	slog_note_l("IN-READER: Starting addition of a new IN read socket [%d] to array", fd);
	TESTP_VOID(core);
	if (!pepa_core_is_valid(core)) {
		slog_error_l("Core structure is invalid");
		return;
	}

	if(NULL == core->in_reading_sockets.sockets) {
		slog_error_l("core->in_reading_sockets.sockets == NULL");
		return;
	}

	for (i = 0; i < core->in_reading_sockets.number; i++) {
		if (EMPTY_SLOT == core->in_reading_sockets.sockets[i]) {
			core->in_reading_sockets.sockets[i] = fd;
			slog_note_l("IN-READER: Added socket %d to slot %d", core->in_reading_sockets.sockets[i], i);
			return;
		}
	}
	slog_error_l("Can not to add of a new IN read socket [%d] to array", fd);
}

/**
 * @author Sebastian Mountaniol (1/17/24)
 * @brief Close and remove IN reading socket
 * @param pepa_core_t* core  Core structure
 * @param const int fd    Invalid socket file descriptor 
 * @details When one of the IN reading sockets is disconnected,
 *          this function will close it and remove it from the
 *          array of opened IN reading sockets. NOTE:
 *          This function does not remove the file descriptor
 *          from the epoll set, it should  be done before this
 *          function is called.
 */
void pepa_in_reading_sockets_close_rm(pepa_core_t *core, const int fd)
{
	int i;
	TESTP_VOID(core);
	if (!pepa_core_is_valid(core)) {
		slog_error_l("Core structure is invalid");
		return;
	}
	TESTP_VOID(core->in_reading_sockets.sockets);
	for (i = 0; i < core->in_reading_sockets.number; i++) {
		if (fd == core->in_reading_sockets.sockets[i]) {
			slog_note_l("IN-READER: Going to close in reading socket %d port %d",
						core->in_reading_sockets.sockets[i],
						pepa_find_socket_port(core->in_reading_sockets.sockets[i]));

			pepa_reading_socket_close(core->in_reading_sockets.sockets[i], "IN-FORWARD");
			slog_note_l("IN-READER: Closed and remove socket %d", core->in_reading_sockets.sockets[i]);
			core->in_reading_sockets.sockets[i] = EMPTY_SLOT;
			return;
		}
	}
	slog_note_l("IN-READER: Could not close and removed socket %d", fd);
}

