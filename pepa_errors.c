#include <stdlib.h>
#include "pepa_errors.h"

#if 0
typedef enum {
	PEPA_ERR_OK = 0,

	/* Memory related errors */
	PEPA_ERR_NULL_POINTER,
	PEPA_ERR_ALLOCATION,

	/* Core related errors */
	PEPA_ERR_INIT_MITEX,
	PEPA_ERR_DESTROY_MITEX,
	PEPA_ERR_CORE_CREATE,
	PEPA_ERR_CORE_DESTROY,

	/* Network related errors */
	PEPA_ERR_INVALID_ADDRESS,
	PEPA_ERR_ADDRESS_FORMAT,
	PEPA_ERR_SOCKET_CREATION,
	PEPA_ERR_SOCKET_BIND,
	PEPA_ERR_SOCKET_LISTEN,
	PEPA_ERR_SOCK_CONNECT,
	PEPA_ERR_CONVERT_ADDR,


	/* Data transfer */
	PEPA_ERR_SELECT_EXCEPTION_LEFT,
	PEPA_ERR_SELECT_EXCEPTION_RIGHT,
	PEPA_ERR_SELECT_ABNORMAL,
	PEPA_ERR_SOCKET_READ,
	PEPA_ERR_SOCKET_READ_CLOSED,
	PEPA_ERR_SOCKET_WRITE,
	PEPA_ERR_SOCKET_WRITE_CLOSED,

} pepa_error_t;
#endif

const char *pepa_error_code_to_str(int code)
{
	switch (abs(code)) {
	case PEPA_ERR_OK: return "OK";

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
	case PEPA_ERR_SOCKET_LISTEN: return "Can not listen on socket";
	case PEPA_ERR_SOCK_CONNECT: return "Can not connect socket";
	case PEPA_ERR_CONVERT_ADDR: return "Can not convert IP address from string to binary";

	/* Data transfer */
	case PEPA_ERR_SELECT_EXCEPTION_LEFT: return "An exception happened on 'left' file descriptor";
	case PEPA_ERR_SELECT_EXCEPTION_RIGHT: return "An exception happened on 'right' file descriptor";
	case PEPA_ERR_SOCKET_READ_CLOSED: return "Socked closed when tried to read from";
	case PEPA_ERR_SOCKET_WRITE: return "Socked closed when tried to write to";
	case PEPA_ERR_SOCKET_WRITE_CLOSED: return "Right socked closed when tried to write to";
	default: return "Unknown error code";
	}

	return "You should never see it";
}

