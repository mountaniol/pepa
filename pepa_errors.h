#ifndef _PEPA_ERRORS_H__
#define _PEPA_ERRORS_H__

typedef enum {
	PEPA_ERR_OK = 0,

	/* Not so errors */
	PEPA_ERR_EVENT, /* This is code telling an event received on event fd  */
	PEPA_ERR_STOP, /* This is code telling to thread to stop */

	/* Situative statuses */
	PEPA_ERR_THREAD_SHVA_DOWN,
	PEPA_ERR_THREAD_IN_DOWN,
	PEPA_ERR_THREAD_IN_SOCKET_RESET,
	PEPA_ERR_THREAD_OUT_DOWN,

	/* Memory related errors */
	PEPA_ERR_NULL_POINTER,
	PEPA_ERR_BUF_ALLOCATION,

	/* File related */
	PEPA_ERR_CANNOT_CLOSE, /* Can not close file or socket */
	PEPA_ERR_FILE_DESCRIPTOR, /* Invalid file (or socket) descriptor */
	PEPA_ERR_EPOLL_CANNOT_ADD, /* Can not add file desriptor to epoll */

	/* String and parse related */
	PEPA_ERR_INVALID_INPUT,

	/* Core related errors */
	PEPA_ERR_INIT_MITEX,
	PEPA_ERR_DESTROY_MITEX,
	PEPA_ERR_CORE_CREATE,
	PEPA_ERR_CORE_DESTROY,

	/* Network related errors */
	PEPA_ERR_ADDRESS_FORMAT,
	PEPA_ERR_SOCKET_CREATION,
	PEPA_ERR_SOCKET_BIND,
	PEPA_ERR_SOCKET_IN_USE,
	PEPA_ERR_SOCKET_LISTEN,
	PEPA_ERR_SOCK_CONNECT,
	PEPA_ERR_CONVERT_ADDR,
	PEPA_ERR_SOCKET_CLOSE,
	PEPA_ERR_CANNOT_SHUTDOWN,

	/* Data transfer */
	PEPA_ERR_SELECT_EXCEPTION_LEFT,
	PEPA_ERR_SELECT_EXCEPTION_RIGHT,
	PEPA_ERR_SELECT_ABNORMAL,
	PEPA_ERR_SOCKET_READ,
	PEPA_ERR_BAD_SOCKET_READ,
	PEPA_ERR_BAD_SOCKET_WRITE,
	PEPA_ERR_SOCKET_READ_CLOSED,
	PEPA_ERR_SOCKET_WRITE,
	PEPA_ERR_SOCKET_WRITE_CLOSED,

	/* Treads related */
	PEPA_ERR_THREAD_CANNOT_CREATE,
	PEPA_ERR_THREAD_DEAD,
	PEPA_ERR_THREAD_DETOUCH,

	/* Socket related */
	PEPA_ERR_SOCKET_IN_LISTEN_DOWN,
	
	/* Unknown error number */
	PEPA_ERR_ERROR_OUT_OF_RANGE,
} pepa_error_t;

__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief Convert PEPA internal error code to string describing
 *  	  the code
 * @param int code  The code to convert to string
 * @return const char* Constant string describing the error code
 * @details The code can be negative or positive, the absolute
 *  		value will be used.
 * The returned string is constant and should not be freed().
 */
const char *pepa_error_code_to_str(int code);
#endif /* _PEPA_ERRORS_H__ */
