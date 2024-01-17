#include <stdlib.h>
#include "pepa_errors.h"

const char *pepa_error_code_to_str(int32_t code)
{
	pepa_error_t enum_code = (pepa_error_t) abs(code);
	switch (enum_code) {
	case PEPA_ERR_OK: return "OK";
	case PEPA_ERR_BUF_ALLOCATION: return "PEPA_ERR_BUF_ALLOCATION: Can not allocate buf_t";

	/* File related */
	case PEPA_ERR_CANNOT_CLOSE: return "PEPA_ERR_CANNOT_CLOSE: Given file descriptor looks valid but cannot be closed";
	case PEPA_ERR_FILE_DESCRIPTOR: return "PEPA_ERR_FILE_DESCRIPTOR: An invalid file descriptor, for example, a negative";
	case PEPA_ERR_EPOLL_CANNOT_ADD: return "PEPA_ERR_EPOLL_CANNOT_ADD: epoll() can not add given file descriptor to its watch set";

	/* String and parse related */
	case PEPA_ERR_INVALID_INPUT: return "PEPA_ERR_INVALID_INPUT: Invalid input given to parser";

	/* Core related errors */
	case PEPA_ERR_INIT_MITEX: return "PEPA_ERR_INIT_MITEX: Can not init semaphore object";
	case PEPA_ERR_CORE_CREATE: return "PEPA_ERR_CORE_CREATE: Can not create core structure";

	/* Network related errors */
	case PEPA_ERR_ADDRESS_FORMAT: return "PEPA_ERR_ADDRESS_FORMAT: IP address format is invalid";
	case PEPA_ERR_SOCKET_CREATION: return "PEPA_ERR_SOCKET_CREATION: Can not create socket";
	case PEPA_ERR_SOCKET_BIND: return "PEPA_ERR_SOCKET_BIND: Can not bind socket";
	case PEPA_ERR_SOCKET_IN_USE: return "PEPA_ERR_SOCKET_IN_USE: Socket in use";
	case PEPA_ERR_SOCKET_LISTEN: return "PEPA_ERR_SOCKET_LISTEN: Can not listen on socket";
	case PEPA_ERR_SOCK_CONNECT: return "PEPA_ERR_SOCK_CONNECT: Can not connect socket";
	case PEPA_ERR_CONVERT_ADDR: return "PEPA_ERR_CONVERT_ADDR: Can not convert IP address from string to binary";
	
	case PEPA_ERR_BAD_SOCKET_READ: return "PEPA_ERR_BAD_SOCKET_READ: Could not read from socket; this is not because socket is closed";
	case PEPA_ERR_BAD_SOCKET_WRITE: return "PEPA_ERR_BAD_SOCKET_WRITE: Could not write to socket; this is not because socket is closed";
	case PEPA_ERR_THREAD_CANNOT_CREATE: return "PEPA_ERR_THREAD_CANNOT_CREATE: Cannot create socket";
	case PEPA_ERR_THREAD_DEAD: return "PEPA_ERR_THREAD_DEAD: The thread is dead";
	case PEPA_ERR_THREAD_DETOUCH: return "PEPA_ERR_THREAD_DETOUCH: Cannot detouch new thread";

	/* Unknown error number */
	case PEPA_ERR_ERROR_OUT_OF_RANGE: return "PEPA_ERR_ERROR_OUT_OF_RANGE: Unknown PEPA error code was provided";

	default: return "Unknown error code";
	}

	return "You should never see it";
}

