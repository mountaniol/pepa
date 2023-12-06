#ifndef _PEPA_SOCKET_H__
#define _PEPA_SOCKET_H__

/**
 * @author Sebastian Mountaniol (12/5/23)
 * @brief Open listening TCP/IP coket and return the socket file
 *  	  descriptor
 * @param struct sockaddr_in* s_addr Reusable structure,
 *  			 filled in function but brovided from outside by
 *  			 the caller; This argument can not be NULL
 * @param buf_t *ip_address - String buffer containing ip in
 *  			string form; This argument can be NULL; if it is
 *  			NULL, than INADDR_ANY used
 * @param int port - On what port the socket to be opened
 * @return int File descriptor of opened socket, a negative
 *  	   value otherwise
 * @details
 */
extern int pepa_open_socket(struct sockaddr_in *s_addr, buf_t *ip_address, int port);

#endif _PEPA_SOCKET_H__
