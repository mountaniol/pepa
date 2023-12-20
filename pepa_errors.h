#ifndef _PEPA_ERRORS_H__
#define _PEPA_ERRORS_H__

typedef enum {
	PEPA_ERR_OK = 0,

	/* Not so errors */
	PEPA_ERR_EVENT, /* This is code telling an event received on event fd  */
	PEPA_ERR_STOP, /* This is code telling to thread to stop */

	/* Memory related errors */
	PEPA_ERR_NULL_POINTER,
	PEPA_ERR_ALLOCATION,

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
} pepa_error_t;

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
