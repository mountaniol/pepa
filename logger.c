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

queue_t         *q_buffers      = NULL;
queue_t         *ql             = NULL;
pthread_t       puller_thread;
int             logger_level    = LOGGER_REGULAR;

int             logger_on_off   = 0;
size_t          logger_counter  = 1;

int             num_of_messages = 0;
pthread_mutex_t cond_mutex      = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond            = PTHREAD_COND_INITIALIZER;

#define PRINT_BUF_SZ (512)

typedef struct {
    char *file;
    size_t line;
    int level;
    char *string;
    size_t len;
    size_t buf_size;
} mes_t;

static size_t logger_get_counter(void)
{
    return logger_counter++;
}


static void *buffers_get(size_t len)
{
    void *buf = NULL;

    if (len < PRINT_BUF_SZ) {
        buf = queue_pop_right(q_buffers);
    }

    if (buf) {
        memset(buf, 0, PRINT_BUF_SZ);
        return buf;
    }

    return calloc(PRINT_BUF_SZ, 1);
}

static int buffers_put(void *buf)
{
    TESTP(buf,  -1);
    return queue_push_left(q_buffers,  buf);
}

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
    if (m->string && m->len == PRINT_BUF_SZ) {
        buffers_put(m->string);
        m->string = NULL;
    }

    if (m->string) {
        free(m->string);
    }

    free(m);
}

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
static int logger_init_queue(void)
{
    ql = queue_create();
    if (NULL == ql) {
        slog_error("Can note init printing queue!");
        return -1;
    }
    return 0;
}

static int logger_init_buffers(void)
{
    q_buffers = queue_create();
    if (NULL == q_buffers) {
        slog_error("Can note init buffers queue!");
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

void logger_set_off(void)
{
    logger_on_off = 0;
}

void logger_set_on(void)
{
    logger_on_off = 1;
}

static int logger_is_off(void)
{
    return logger_on_off;
}

static void logger_print_message(mes_t *m)
{
    if (logger_is_off()) {
        usleep(100000);
    }
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
        slog_error("Can note init printing queue!");
        return -1;
    }

    rc = logger_init_buffers();
    if (0 != rc) {
        slog_error("Can note init buffers queue!");
        return -1;
    }

    rc = pthread_create(&puller_thread, NULL, logger_thread, ql);
    if (0 != rc) {
        slog_error("Can note create printing thread!");
        return -2;
    }
    return 0;
}

int logger_stop(void)
{
    return pthread_cancel(puller_thread);
}

__attribute__((format(printf, 4, 5)))
int llog(int level, char *file, size_t line, const char *format, ...)
{
    va_list args;
    int     len     = -1;
    int     rc      = -1;
    char    *string = NULL;

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
        slog_error("Can note calculate string len!");
        return -1;
    }

    len += 10;

    if (len < PRINT_BUF_SZ) {
        // len = PRINT_BUF_SZ;
        string = buffers_get(len);
    }

    if (NULL == string) {
        string = calloc(len + 10,  1);
    } else {
        len = PRINT_BUF_SZ;
    }

    if (NULL == string) {
        slog_error("Can note allocate string!");
        return -2;
    }

    /* Print counter of the message */
    size_t offset = sprintf(string, "[%lu] ", logger_get_counter());

    /* Now print the line */
    va_start(args, format);
    rc = vsnprintf(string + offset, len, format, args);
    va_end(args);
    if (rc < 1) {
        free(string);
        slog_error("Can note print!");
        return -3;
    }

    logger_push(level, string, len, file, line);

    return 0;
}

