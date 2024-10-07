#ifndef LOGGER_H__
#define LOGGER_H__

#include <stdarg.h>

#define LOGGER_BUFFER (4096)

enum {
    LOGGER_NOISY = 1,
    LOGGER_VERBOSE,
    LOGGER_INFO,
    LOGGER_REGULAR,
    LOGGER_WARNING,
    LOGGER_DEBUG,
    LOGGER_ERROR,
    LOGGER_FATAL
};

int logger_init_queue(void);
int logger_start(void);
int logger_stop(void);
void logger_set_level(int level);
int logger_get_level(void);
void logger_push(int level, char *string, size_t len, char *file, const size_t line);


 __attribute__ ((format (printf, 4, 5)))
int llog(int level, char *file, size_t line, const char *format, ...);

#define llog_noisy(format, ...) llog(LOGGER_NOISY, __FILE__, __LINE__, format,  ##__VA_ARGS__)
#define llog_n(format, ...) llog(LOGGER_NOISY, __FILE__, __LINE__, format,  ##__VA_ARGS__)

#define llog_verb(format, ...) llog(LOGGER_VERBOSE, __FILE__, __LINE__, format,  ##__VA_ARGS__)
#define llog_v(format, ...) llog(LOGGER_VERBOSE, __FILE__, __LINE__, format,  ##__VA_ARGS__)

#define llog_info(format, ...) llog(LOGGER_INFO, __FILE__, __LINE__, format,  ##__VA_ARGS__)
#define llog_i(format, ...) llog(LOGGER_INFO, __FILE__, __LINE__, format,  ##__VA_ARGS__)

#define llog_reg(format, ...) llog(LOGGER_REGULAR, __FILE__, __LINE__, format,  ##__VA_ARGS__)
#define llog_r(format, ...) llog(LOGGER_REGULAR, __FILE__, __LINE__, format,  ##__VA_ARGS__)

#define llog_warn(format, ...) llog(LOGGER_WARNING, __FILE__, __LINE__, format,  ##__VA_ARGS__)
#define llog_w(format, ...) llog(LOGGER_WARNING, __FILE__, __LINE__, format,  ##__VA_ARGS__)

#define llog_debug(format, ...) llog(LOGGER_DEBUG, __FILE__, __LINE__, format,  ##__VA_ARGS__)
#define llog_d(format, ...) llog(LOGGER_DEBUG, __FILE__, __LINE__, format,  ##__VA_ARGS__)

#define llog_error(format, ...) llog(LOGGER_ERROR, __FILE__, __LINE__, format,  ##__VA_ARGS__)
#define llog_e(format, ...) llog(LOGGER_ERROR, __FILE__, __LINE__, format,  ##__VA_ARGS__)

#define llog_fatal(format, ...) llog(LOGGER_FATAL, __FILE__, __LINE__, format,  ##__VA_ARGS__)
#define llog_f(format, ...) llog(LOGGER_FATAL, __FILE__, __LINE__, format,  ##__VA_ARGS__)

#endif /* LOGGER_H__ */

