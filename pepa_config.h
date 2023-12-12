#ifndef _PEPA_CONFIG_H_
#define _PEPA_CONFIG_H_

/* Max length of string IP address */
#define IP_LEN   (24)

/* Size of buffer used to copy from fd to fd*/
#define COPY_BUF_SIZE (128)

/* Predefined number of IN clients */
#define PEPA_IN_SOCKETS (1024)

/* Predefined number of OUT clients */
#define PEPA_OUT_SOCKETS (1)

/* Predefined number of SHVA clients */
#define PEPA_SHVA_SOCKETS (1)

#endif /* _PEPA_CONFIG_H_ */
