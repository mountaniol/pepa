#ifndef _PEPA_ERRORS_H__
#define _PEPA_ERRORS_H__

typedef enum {
	PEPA_ERR_OK = 0,

	PEPA_ERR_BUF_ALLOCATION,

	/* File related */
	PEPA_ERR_CANNOT_CLOSE, /* Can not close file or socket */
	PEPA_ERR_FILE_DESCRIPTOR, /* Invalid file (or socket) descriptor */
	PEPA_ERR_EPOLL_CANNOT_ADD, /* Can not add file desriptor to epoll */

	/* String and parse related */
	PEPA_ERR_INVALID_INPUT,

	/* Core related errors */
	PEPA_ERR_INIT_MITEX,
	PEPA_ERR_CORE_CREATE,

	/* Network related errors */
	PEPA_ERR_ADDRESS_FORMAT,
	PEPA_ERR_SOCKET_CREATION,
	PEPA_ERR_SOCKET_BIND,
	PEPA_ERR_SOCKET_IN_USE,
	PEPA_ERR_SOCKET_LISTEN,
	PEPA_ERR_SOCK_CONNECT,
	PEPA_ERR_CONVERT_ADDR,

	/* Data transfer */
	PEPA_ERR_BAD_SOCKET_READ,
	PEPA_ERR_BAD_SOCKET_WRITE,
	PEPA_ERR_BAD_SOCKET_LOCAL, /* Local error on socket, must be reseted */
	PEPA_ERR_BAD_SOCKET_REMOTE, /* Remote error on soket */
	PEPA_ERR_BAD_SOCKET_ERROR, /* Unknown error on socket */

    /* New class of errors, an error per socket */

    PEPA_ERR_SHVA_READ, /**< Can not read from SHVA */
	PEPA_ERR_SHVA_WRITE, /**< Can not write to SHVA */

	PEPA_ERR_OUT_LISTEN, /**< Error on OUT LISTEN socket */
	PEPA_ERR_OUT_READ, /**< Can not read from OUT RW */
	PEPA_ERR_OUT_WRITE, /**< Can not write to OUT RW */

	PEPA_ERR_IN_LISTEN, /**< Error on IN listen */
	PEPA_ERR_IN_READ, /**< Can not read from one of IN read sockets */
	PEPA_ERR_IN_WRITE, /**< Can not readwrite to one of IN read sockets */
    
	/* Treads related */
	PEPA_ERR_THREAD_CANNOT_CREATE,
	PEPA_ERR_THREAD_DEAD,
	PEPA_ERR_THREAD_DETOUCH,

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
