#include <stdlib.h>
#include "pepa_errors.h"

const char *pepa_error_code_to_str(int code)
{
	pepa_error_t enum_code = abs(code);
	switch (enum_code) {
	case PEPA_ERR_OK: return "OK";
	case PEPA_ERR_EVENT: return "And event received on event fd desciptor";
	case PEPA_ERR_STOP: return "Thread must stop and terminate";

	/* Memory related errors */
	case PEPA_ERR_NULL_POINTER: return "An argument is NULL pointer";
	case PEPA_ERR_ALLOCATION: return "Can not allocate memory";

	/* Core related errors */
	case PEPA_ERR_INIT_MITEX: return "Can not init semaphore object";
	case PEPA_ERR_DESTROY_MITEX: return "Can not destroy semaphore object";
	case PEPA_ERR_CORE_CREATE: return "Can not create core structure";
	case PEPA_ERR_CORE_DESTROY: return "Can not destroy core structure";

	/* Network related errors */
	case PEPA_ERR_ADDRESS_FORMAT: return "IP address format is invalid";
	case PEPA_ERR_SOCKET_CREATION: return "Can not create socket";
	case PEPA_ERR_SOCKET_BIND: return "Can not bind socket";
	case PEPA_ERR_SOCKET_IN_USE: return "Socket in use";
	case PEPA_ERR_SOCKET_LISTEN: return "Can not listen on socket";
	case PEPA_ERR_SOCK_CONNECT: return "Can not connect socket";
	case PEPA_ERR_CONVERT_ADDR: return "Can not convert IP address from string to binary";

	/* Data transfer */
	case PEPA_ERR_SELECT_EXCEPTION_LEFT: return "An exception happened on 'left' file descriptor";
	case PEPA_ERR_SELECT_EXCEPTION_RIGHT: return "An exception happened on 'right' file descriptor";
	case PEPA_ERR_SELECT_ABNORMAL: return "select() call finished with an error";
	case PEPA_ERR_SOCKET_READ: return "Data transfer between sockets finished with an error";
	case PEPA_ERR_BAD_SOCKET_READ: return "Could not read from socket; this is not because socket is closed";
	case PEPA_ERR_BAD_SOCKET_WRITE: return "Could not write to socket; this is not because socket is closed";
	case PEPA_ERR_SOCKET_READ_CLOSED: return "Socked closed when tried to read from";
	case PEPA_ERR_SOCKET_WRITE: return "Socked closed when tried to write to";
	case PEPA_ERR_SOCKET_WRITE_CLOSED: return "Right socked closed when tried to write to";
	case PEPA_ERR_THREAD_CANNOT_CREATE: return "Cannot create socket";
	default: return "Unknown error code";
	}

	return "You should never see it";
}

