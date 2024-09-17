#ifndef _PEPA_CONFIG_H_
#define _PEPA_CONFIG_H_

#include "pepa_core.h"

/* The copying buffer default size */
#define COPY_BUF_SIZE_BYTES (64 * 1024)

/* The printing buffer default size */
#define PRINT_BUF_SIZE_BYTES (COPY_BUF_SIZE_BYTES + 1024)

/* Predefined number of IN clients */
#define PEPA_IN_SOCKETS 4096

/* Predefined number of OUT clients */
#define PEPA_OUT_SOCKETS (12)

/* Predefined number of SHVA clients */
#define PEPA_SHVA_SOCKETS (12)

#ifndef PPEPA_MAX
    #define PEPA_MAX(a,b) ( a > b ? a : b)
#endif

/* A default value of an incactive pthread descriptor */
#define PTHREAD_DEAD ((pthread_t)0xBEEFDEAD)

/* Assign this value to closed file descriptor */
#define FD_CLOSED (-1)

/* Default time of monithor thread activity frequency */
#define MONITOR_TIMEOUT_USEC (500000)

/* SLOG flags; here we define accumuulative log levels */

#define SLOG_LEVEL_FATAL (SLOG_FATAL)
#define SLOG_LEVEL_TRACE (SLOG_TRACE | SLOG_LEVEL_FATAL)
#define SLOG_LEVEL_ERROR (SLOG_ERROR | SLOG_LEVEL_TRACE)
#define SLOG_LEVEL_DEBUG (SLOG_DEBUG | SLOG_LEVEL_ERROR)
#define SLOG_LEVEL_WARN (SLOG_WARN | SLOG_LEVEL_DEBUG)
#define SLOG_LEVEL_INFO (SLOG_INFO | SLOG_LEVEL_WARN)
#define SLOG_LEVEL_NOTE (SLOG_NOTE | SLOG_LEVEL_INFO)

#define SLOG_LEVEL_1 SLOG_LEVEL_FATAL
#define SLOG_LEVEL_2 SLOG_LEVEL_TRACE
#define SLOG_LEVEL_3 SLOG_LEVEL_ERROR
#define SLOG_LEVEL_4 SLOG_LEVEL_DEBUG
#define SLOG_LEVEL_5 SLOG_LEVEL_WARN
#define SLOG_LEVEL_6 SLOG_LEVEL_INFO
#define SLOG_LEVEL_7 SLOG_LEVEL_NOTE

int pepa_read_config(pepa_core_t *core);

#endif /* _PEPA_CONFIG_H_ */
