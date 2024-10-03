#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>

#include "slog/src/slog.h"
#include "queue.h"
#include "debug.h"
#include "logger.h"

/*** INTERNAL STRUCTURES AND DEFINITIONS *****/

int             num_of_messages = 0;
pthread_mutex_t cond_mutex      = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond            = PTHREAD_COND_INITIALIZER;



typedef struct {
    char *file;
    size_t line;
    int level;
    char *string;
    size_t len;
} mes_t;

static mes_t *message_alloc(int level, char *string, const size_t len, char *file, size_t line)
{
    mes_t *m = malloc(sizeof(mes_t));
    if (NULL == m) {
        return NULL;
    }

    m->level = level;
    m->string = string;
    m->len = len;
    m->file = file;
    m->line = line;
    return m;
}

static void message_free(mes_t *m)
{
    TESTP_VOID(m);
    if (m->string) {
        free(m->string);
    }

    free(m);
}

queue_t   *ql           = NULL;
pthread_t puller_thread;
int       logger_level  = LOGGER_REGULAR;

/**** API *****/

void logger_set_level(int level)
{
    logger_level = level;
}

int logger_get_level(void)
{
    return logger_level;
}


/* Create logger queue */
int logger_init_queue(void)
{
    ql = queue_create();
    if (NULL == ql) {
        return -1;
    }
    return 0;
}

void logger_push(int level, char *string, size_t len, char *file, const size_t line)
{
    mes_t *m = message_alloc(level, string, len, file, line);
    TESTP_VOID(m);

    queue_push_left(ql, m);

    /* Emit signal to the printing thread that there are waiting messages in the queue */
    pthread_mutex_lock(&cond_mutex);

    num_of_messages++;

    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&cond_mutex);
}

static void logger_print_message(mes_t *m)
{
    // printf("SEB: [%d] %s\n", m->level, m->string);
#if 0

    #define slog_note_l(fmt, ...)  slog_note(fmt " [%s +%d]" , ##__VA_ARGS__,  __func__, __LINE__)
    #define slog_info_l(fmt, ...)  slog_info(fmt " [%s +%d]" , ##__VA_ARGS__,  __func__, __LINE__)
    #define slog_warn_l(fmt, ...)  slog_warn(fmt " [%s +%d]" , ##__VA_ARGS__,  __func__, __LINE__)
    #define slog_debug_l(fmt, ...) slog_debug(fmt " [%s +%d]" , ##__VA_ARGS__,  __func__, __LINE__)
    #define slog_error_l(fmt, ...) slog_error(fmt " [%s +%d]" , ##__VA_ARGS__,  __func__, __LINE__)
    #define slog_trace_l(fmt, ...) slog_trace(fmt " [%s +%d]" , ##__VA_ARGS__,  __func__, __LINE__)
    #define slog_fatal_l(fmt, ...) slog_fatal(fmt " [%s +%d]" , ##__VA_ARGS__,  __func__, __LINE__)
#endif

    switch (m->level) {
    case LOGGER_NOISY:
        slog_note("%s [%s +%lu]", m->string, m->file, m->line);
        break;
    case LOGGER_VERBOSE:
        slog_info("%s [%s +%lu]", m->string, m->file, m->line);
        break;
    case LOGGER_INFO:
        slog_info("%s [%s +%lu]", m->string, m->file, m->line);
        break;
    case LOGGER_REGULAR:
        slog_info("%s [%s +%lu]", m->string, m->file, m->line);
        break;
    case LOGGER_WARNING:
        slog_warn("%s [%s +%lu]", m->string, m->file, m->line);
        break;
    case LOGGER_DEBUG:
        slog_debug("%s [%s +%lu]", m->string, m->file, m->line);
        break;
    case LOGGER_ERROR:
        slog_error("%s [%s +%lu]", m->string, m->file, m->line);
        break;
    case LOGGER_FATAL:
        slog_fatal("%s [%s +%lu]", m->string, m->file, m->line);
        break;
    default:
        slog_fatal("%s [%s +%lu]", m->string, m->file, m->line);
    }
}

static void *logger_thread(void *arg)
{
    mes_t   *m;
    queue_t *qu = arg;
    if (NULL == qu) {
        return NULL;
    }

    do {
        /* Wait conditione, i.e. when a message is pushed to the queue */
        pthread_mutex_lock(&cond_mutex);
        pthread_cond_wait(&cond, &cond_mutex);

        /* If the number of messages is 0, we should continue sleeping */
        if (num_of_messages < 1) {
            continue;
        }

        /* All right, we know that there are messages; now we reset the variable and release the mutes,
           so writer should not waiting */
        num_of_messages = 0;
        pthread_mutex_unlock(&cond_mutex);

        /* While there are messages, read all of them and print them / log them */
        do {

            m = queue_pop_right(qu);
            if (m) {
                logger_print_message(m);
                message_free(m);
            }
        } while (NULL != m);


    } while (1);

    return NULL;

}

int logger_start(void)
{
    int rc = logger_init_queue();
    if (0 != rc) {
        return -1;
    }

    rc = pthread_create(&puller_thread, NULL, logger_thread, ql);
    if (0 != rc) {
        return -2;
    }
    return 0;
}

int logger_stop(void)
{
    return pthread_cancel(puller_thread);
}

__attribute__((format(printf, 4, 0)))
int llog(int level, char *file, size_t line, const char *format, ...)
{
    va_list args;
    int     len;
    char    *string;

    if (NULL == ql) {
        abort();
    }

    if (level < logger_level) {
        return 0;
    }

    /* Measure string lengh */
    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    if (len < 1) {
        return -1;
    }

    string = calloc(len + 1,  1);

    if (NULL == string) {
        return -2;
    }

    /* Now print the line */
    va_start(args, format);
    len = vsnprintf(string, len, format, args);
    va_end(args);
    if (len < 1) {
        free(string);
        return -3;
    }

    logger_push(level, string, len, file, line);

    return 0;
}

