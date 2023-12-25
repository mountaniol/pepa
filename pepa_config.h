#ifndef _PEPA_CONFIG_H_
#define _PEPA_CONFIG_H_

/* Size of buffer used to copy from fd to fd*/
#define COPY_BUF_SIZE (128)

/* Predefined number of IN clients */
#define PEPA_IN_SOCKETS (1024)

/* Predefined number of OUT clients */
#define PEPA_OUT_SOCKETS (1)

/* Predefined number of SHVA clients */
#define PEPA_SHVA_SOCKETS (1)

#ifndef PPEPA_MAX
	#define PEPA_MAX(a,b) ( a > b ? a : b)
#endif

#define PTHREAD_DEAD ((pthread_t)0xBEEFDEAD)

#define MONITOR_TIMEOUT_USEC (500000)

#define SLOG_LEVEL_FATAL (SLOG_FATAL)
#define SLOG_LEVEL_TRACE (SLOG_FATAL | SLOG_LEVEL_FATAL)
#define SLOG_LEVEL_ERROR (SLOG_ERROR | SLOG_LEVEL_TRACE)
#define SLOG_LEVEL_DEBUG (SLOG_DEBUG | SLOG_LEVEL_ERROR)
#define SLOG_LEVEL_WARN (SLOG_WARN | SLOG_LEVEL_DEBUG)
#define SLOG_LEVEL_INFO (SLOG_WARN | SLOG_LEVEL_WARN)
#define SLOG_LEVEL_NOTE (SLOG_NOTE | SLOG_LEVEL_INFO)

#define SLOG_LEVEL_1 SLOG_LEVEL_FATAL
#define SLOG_LEVEL_2 SLOG_LEVEL_TRACE
#define SLOG_LEVEL_3 SLOG_LEVEL_ERROR
#define SLOG_LEVEL_4 SLOG_LEVEL_DEBUG
#define SLOG_LEVEL_5 SLOG_LEVEL_WARN
#define SLOG_LEVEL_6 SLOG_LEVEL_INFO
#define SLOG_LEVEL_7 SLOG_LEVEL_NOTE

#endif /* _PEPA_CONFIG_H_ */
