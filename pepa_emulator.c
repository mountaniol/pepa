#define _GNU_SOURCE
#include <pthread.h>
#include <sys/param.h>
#include <unistd.h> /* For read() */
#include <sys/epoll.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "murmur3.h"
#include "zhash2.h"
#include "pepa_emulator.h"
#include "buf_t/buf_t.h"
#include "pepa_config.h"
#include "pepa_core.h"
#include "slog/src/slog.h"
#include "pepa_errors.h"
#include "pepa_parser.h"
#include "pepa_server.h"
#include "pepa_socket_common.h"
#include "pepa_state_machine.h"
#include "pepa_ticket_id.h"
#include "pepa_utils.h"

static void emu_compare_sent_and_received_buffers(emu_t *emu, buf_t *buf_recv);

/* These flags ddefine either IN and TX should transfer. */
static int enable_in_tx = 1;
static int enable_shva_tx = 1;

/* Pipe between OUT threads */
#define PEPA_OUT_R (0)
#define PEPA_OUT_W (1)
int pipe_out[2] = {-1, -1};


emu_t *emu = NULL;

#if 1

//#define SHUTDOWN_DIVIDER (100003573)
//#define SHUTDOWN_DIVIDER (1000035)
    #define SHUTDOWN_DIVIDER (100035)
//#define SHUTDOWN_DIVIDER (10000357377)
//#define SHVA_SHUTDOWN_DIVIDER (10000357)
//#define SHVA_SHUTDOWN_DIVIDER (1000035)
    #define SHVA_SHUTDOWN_DIVIDER (100037)
    #define SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHUTDOWN_DIVIDER))
    #define SHVA_SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHVA_SHUTDOWN_DIVIDER))
#else

                #define SHOULD_EMULATE_DISCONNECT() (0)

// #define SHVA_SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHVA_SHUTDOWN_DIVIDER))

                #define SHVA_SHOULD_EMULATE_DISCONNECT() (0)
#endif
                #define RX_TX_PRINT_DIVIDER (100000)

                #define PEPA_MIN(a,b) ((a<b) ? a : b )

/* Keep here PIDs of IN threads */

const char *lorem_ipsum = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.\0";
uint64_t lorem_ipsum_len = 0;

// buf_t      *lorem_ipsum_buf = NULL;

/*** EMU struct implimentation ****/

static const char *st_to_str(int st)
{
    switch (st) {
        case ST_NO:
            return ("ST_NO");
        case ST_STOPPED:
            return ("ST_STOPPED");
        case ST_STARTING:
            return ("ST_STARTING");
        case ST_WAITING:
            return ("ST_WAITING");
        case ST_RUNNING:
            return ("ST_RUNNING");
        case ST_READY:
            return ("ST_READY");
        case ST_RESET:
            return ("ST_RESET");
        case ST_EXIT:
            return ("ST_EXIT");
        default:
            return ("UNKNOWN");
    }
    return ("UNKNOWN");
}

static int emu_t_free(emu_t *emu)
{

    TESTP(emu, -1);

    pthread_mutex_lock(&emu->lock);
    if (emu->in_number > 0 && emu->in_ids) {
        TFREE(emu->in_ids);
        TFREE(emu->in_stat);
        TFREE(emu->cnt_in_sent);
        TFREE(emu->cnt_in_recv);
    }

    pthread_mutex_destroy(&emu->lock);
    pthread_mutex_destroy(&emu->in_threads_lock);

    zfree_hash_table(emu->zhash_bufs);

    TFREE(emu);
    return (0);
}

static emu_t *emu_t_allocate(int num_of_in, pepa_core_t *core)
{
    emu_t *emu_local = calloc(sizeof(emu_t), 1);
    TESTP(emu_local, NULL);

    emu_local->in_number = num_of_in;

    /* Allocate pthread_t array for IN threads */
    emu_local->in_ids = calloc(sizeof(pthread_t), emu_local->in_number);
    emu_local->in_stat = calloc(sizeof(char), emu_local->in_number);

    emu_local->cnt_in_sent = calloc(sizeof(emu_cnt_t), emu_local->in_number);
    emu_local->cnt_in_recv = calloc(sizeof(emu_cnt_t), emu_local->in_number);

    for (size_t idx = 0; idx < emu_local->in_number; idx++) {
        emu_local->cnt_in_sent[idx] = (idx + 1) * 1000;
        emu_local->cnt_in_recv[idx] = 0;
    }


    if (NULL == emu_local->in_ids || NULL == emu_local->in_stat) {
        slog_error_l("Can not allocate emu_t: emu->in_ids = %p, emu->in_stat = %p",
                     emu_local->in_ids, emu_local->in_stat);
        emu_t_free(emu_local);
        return (NULL);
    }

    for (size_t idx = 0; idx < emu_local->in_number; idx++) {
        emu_local->in_ids[idx] = FD_CLOSED;
        emu_local->in_stat[idx] = ST_NO;
    }

    /* Set thread statuses to ST_NO */
    emu_local->shva_read_status = ST_NO;
    emu_local->shva_write_status = ST_NO;
    emu_local->shva_main_status = ST_NO;
    emu_local->out_status = ST_NO;

    emu_local->core = core;

    int rc = pthread_mutex_init(&emu_local->lock, NULL);
    if (rc) {
        slog_error_l("Can not init pthread lock, stop");
        emu_t_free(emu_local);
        abort();
    }

    rc = pthread_mutex_init(&emu_local->in_threads_lock, NULL);
    if (rc) {
        slog_error_l("Can not init pthread lock, stop");
        emu_t_free(emu_local);
        abort();
    }

    emu_local->zhash_bufs = zcreate_hash_table();

    if (NULL == emu_local->zhash_bufs) {
        slog_error_l("Can not init pthread lock, stop");
        emu_t_free(emu_local);
        abort();
    }

    return (emu_local);
}

static int emu_lock(emu_t *emu)
{
    TESTP(emu, -1);
    return (pthread_mutex_lock(&emu->lock));
}

static int emu_unlock(emu_t *emu)
{
    TESTP(emu, -1);
    return (pthread_mutex_unlock(&emu->lock));
}

static void emu_print_transition(const char *name, int prev_st, int cur_st)
{
    slog_note_l("[EMU CONTROL] %s: %s -> %s", name, st_to_str(prev_st), st_to_str(cur_st));
}

static int emu_get_shva_read_status(emu_t *emu)
{
    emu_lock(emu);
    int st = emu->shva_read_status;
    emu_unlock(emu);

    return (st);
}

static int emu_get_shva_write_status(emu_t *emu)
{
    emu_lock(emu);
    int st = emu->shva_write_status;
    emu_unlock(emu);

    return (st);
}

static void emu_set_shva_main_status(emu_t *emu, const int status)
{
    emu_lock(emu);
    int prev_st = emu->shva_main_status;
    emu->shva_main_status = status;
    emu_unlock(emu);

    emu_print_transition("SHVA Socket", prev_st, status);
}

static int emu_get_shva_main_status(emu_t *emu)
{
    emu_lock(emu);
    int st = emu->shva_main_status;
    emu_unlock(emu);

    return (st);
}

static void emu_set_out_status(emu_t *emu, const int status)
{
    emu_lock(emu);
    int prev_st = emu->out_status;
    emu->out_status = status;
    emu_unlock(emu);

    emu_print_transition("OUT", prev_st, status);
}

static int emu_get_out_status(emu_t *emu)
{
    emu_lock(emu);
    int st = emu->out_status;
    emu_unlock(emu);

    return (st);
}

static void emu_set_in_status(emu_t *emu, const int in_num, const int status)
{
    char name[64];

    emu_lock(emu);
    int prev_st = emu->in_stat[in_num];
    emu->in_stat[in_num] = status;
    emu_unlock(emu);

    sprintf(name, "IN[%d]", in_num);
    emu_print_transition(name, prev_st, status);
}

static int emu_get_in_status(emu_t *emu, const int in_num)
{
    emu_lock(emu);
    int st = emu->in_stat[in_num];
    emu_unlock(emu);

    return (st);
}

static void emu_set_all_in(emu_t *emu, const int status)
{
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        emu_set_in_status(emu, idx, status);
    }
}

static void emu_set_all_in_state_to_state(emu_t *emu, const int from, const int to)
{
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        if (from == emu_get_in_status(emu, idx)) {}
        emu_set_in_status(emu, idx, to);
    }
}

static int emu_if_in_all_have_status(emu_t *emu, const int status)
{
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        if (status != emu_get_in_status(emu, idx)) {
            return (0);
        }
    }

    return (1);
}

static int emu_if_in_any_have_status(emu_t *emu, const int status)
{
    for (size_t idx = 0; idx < emu->in_number; idx++) {
        if (status == emu_get_in_status(emu, idx)) {
            return (1);
        }
    }
    return (0);
}


/*** SIGNALS ****/

static void pthread_block_signals(const char *name)
{
    sigset_t set;
    sigfillset(&set);
    int rc = pthread_sigmask(SIG_SETMASK, &set, NULL);
    if (0 != rc) {
        slog_error_l("Could not set pthread signal blocking for thread %s", name);
    }
}

static void close_emulatior(void)
{
    emu->should_exit = 1;
}

/* Catch Signal Handler function */
static void signal_callback_handler(int signum, __attribute__((unused)) siginfo_t *info, __attribute__((unused))void *extra)
{
    printf("Caught signal %d\n", signum);
    if (signum == SIGINT) {
        printf("Caught signal SIGINT: %d\n", signum);
        close_emulatior();
        exit(0);
    }
}

static void emu_set_int_signal_handler(void)
{
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = signal_callback_handler;
    sigaction(SIGINT, &action, NULL);
}

/*** UTILITIES ****/

static void pepa_emulator_disconnect_mes(const char *name)
{
    slog_warn(" ");
    slog_warn(" ");
    slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_warn("EMU: Emulating %s disconnect", name);
    slog_warn("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_warn(" ");
    slog_warn(" ");
}

/*** BUFFERS ****/

/* Find offset of header inside of the buffer */
static ssize_t emu_buf_offset(buf_t *buf)
{
    buf_head_t *head = NULL;
    ssize_t offset = 0;
    TESTP(buf, -1);
    TESTP(buf->data, -1);

    do {
        if ((size_t)buf->used < sizeof(buf_head_t) + offset) {
            slog_error_l("Wrong buffer size: buf->used (%ld) < sizeof(buf_head_t) (%lu) + offset (%ld)",
                         buf->used, sizeof(buf_head_t), offset);
            abort();
        }
        head = BUF_HEAD(buf, offset);

        /* Is the header in the beginning of the buf->data?  */
        if (BUF_HEADER_START == head->start && BUF_HEADER_MARK == head->mark) {
            return (offset);
        }
        offset += sizeof(pepa_ticket_t);
    } while (offset <= (ssize_t)(sizeof(pepa_ticket_t) * 2));

    slog_error_l("Can not find offset: tested up to offset = %ld; head->start = %X",
                 offset, head->start);
    abort();

    return (-1);
}

/* Find offset of header inside of the buffer */
static ssize_t emu_buf_data_offset(buf_t *buf)
{
    TESTP(buf, -1);
    TESTP(buf->data, -1);
    ssize_t offset = emu_buf_offset(buf);
    if (offset < 0) {
        return (-1);
    }

    return (offset + sizeof(buf_head_t));
}

static ssize_t emu_buf_get_data_len(buf_t *buf)
{
    ssize_t offset = emu_buf_offset(buf);
    if (offset < 0) {
        return (-1);
    }

    // return (buf->used - offset - sizeof(buf_head_t));
    return (buf->used - sizeof(buf_head_t));
    // return (buf->used - offset);
}

static buf_head_t *emu_buf_get_head(buf_t *buf)
{
    ssize_t offset = emu_buf_offset(buf);
    if (offset < 0) {
        slog_error_l("can not find offset");
        return (NULL);
    }

    return (BUF_HEAD(buf, offset));
}

static char *emu_buf_get_data(buf_t *buf)
{
    ssize_t offset = emu_buf_data_offset(buf);
    if (offset < 0) {
        return (NULL);
    }
    return (buf->data + offset);
}

/**
 * @author Sebastian Mountaniol (26/10/2024)
 * @brief Add sent buffer to the zhash
 * @param emu_t * emu emu_t struct
 * @param buf_t * buf buffer to add; the buf_head_t->ticket is used as the key
 * @return int 0 if added, -1 if one of params is NULL
 * @details
 */
static int emu_save_buf_to_zhash(emu_t *emu, buf_t *buf)
{
    TESTP_ASSERT(emu, "EMU is NULL");
    TESTP_ASSERT(buf, "buf_t is NULL");
    TESTP_ASSERT(buf->data, "buft->data is NULL");

    // buf_head_t *head = BUF_HEAD(buf, 0);
    buf_head_t *head = emu_buf_get_head(buf);
    TESTP_ASSERT(head, "head is NULL");

    emu_lock(emu);
    zhash_insert_by_int(emu->zhash_bufs, head->cnt_global, buf);
    emu->zhash_count++;
    emu_unlock(emu);
    return (0);
}

/**
 * @author Sebastian Mountaniol (26/10/2024)
 * @brief Extract a saved buffer from zhash by the 'ticket' key
 * @param emu_t * emu emu_t struct
 * @param pepa_ticket_t ticket Ticket to search buf_t by
 * @return buf_t* Pointer to a saved buffer if found; NULL otherwise
 * @details
 */
static buf_t *emu_get_buf_from_zhash(emu_t *emu, emu_cnt_t cointer)
{
    buf_t *buf = NULL;
    TESTP(emu, NULL);
    emu_lock(emu);
    buf = zhash_extract_by_int(emu->zhash_bufs, cointer);
    if (buf) {
        emu->zhash_count--;
    }
    emu_unlock(emu);
    return (buf);
}

static const char *src_num_to_str(int src, int instance)
{
    static char string[64] = {0};
    if (BUF_SRC_IN == src) {
        sprintf(string, "IN[%d]", instance);
    }

    if (BUF_SRC_SHVA == src) {
        sprintf(string, "SHVA");
    }

    return (string);
}

static size_t emu_random_buf_size(void)
{
    pepa_core_t *core = pepa_get_core();

    size_t buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
    buf_size += sizeof(buf_head_t);

    if (buf_size > core->emu_max_buf) {
        buf_size = core->emu_max_buf;
    }

    if (buf_size < core->emu_min_buf) {
        buf_size = core->emu_min_buf;
    }

    if (buf_size < sizeof(buf_head_t) * 2) {
        buf_size = sizeof(buf_head_t) * 2;
    }

    if (buf_size > core->emu_max_buf) {
        slog_fatal_l("Calculated buf size (%zu) > core->emu_max_buf (%zu)", buf_size, core->emu_max_buf);
        abort();
    }

    return (buf_size);
}

pthread_mutex_t emu_set_header_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pepa_ticket_t global_ticket = 17;
emu_cnt_t global_cnt = 0;
pthread_mutex_t global_cnt_lock = PTHREAD_MUTEX_INITIALIZER;

static emu_cnt_t emu_get_global_cnt(void)
{
    emu_cnt_t ret;
    pthread_mutex_lock(&global_cnt_lock);
    ret = global_cnt;
    global_cnt++;
    pthread_mutex_unlock(&global_cnt_lock);
    return (ret);
}

static void emu_set_header_buffer(buf_t *buf, const uint64_t cnt, const uint32_t src, const uint32_t instance)
{
    pepa_ticket_t ticket;

    pthread_mutex_lock(&emu_set_header_buffer_mutex);
    global_ticket = pepa_gen_ticket(global_ticket);
    ticket = global_ticket;
    pthread_mutex_unlock(&emu_set_header_buffer_mutex);

    buf_head_t *head = BUF_HEAD(buf, 0);
    head->cnt = cnt;
    head->cnt_global = emu_get_global_cnt();
    head->src = src;
    head->instance = instance;
    head->mark = BUF_HEADER_MARK;
    head->start = BUF_HEADER_START;
    ticket = pepa_gen_ticket(ticket);
    head->ticket = ticket;

    /* Increase number of used bytes */
    buf->used += sizeof(buf_head_t);
}

static void emu_set_header_len(buf_t *buf, uint32_t len)
{
    buf_head_t *head = BUF_HEAD(buf, 0);
    head->len = len;
}

// Simple additive checksum for a char stream, outputting uint64_t
static uint64_t simple_checksum(const char *data, size_t length)
{
    uint64_t checksum = 0;
    for (size_t i = 0; i < length; ++i) {
        checksum += (unsigned char)data[i];  // Sum up each byte, cast to unsigned char to avoid sign extension
    }
    return (checksum);
}

// static uint64_t emu_calc_checksum(buf_t *buf, size_t offset)
static uint64_t emu_calc_checksum(buf_t *buf)
{
    emu_checksum_t checksum = 0;
    const char *data = emu_buf_get_data(buf);
    //buf_head_t *head = emu_buf_get_head(buf);
    TESTP(data, 0);

    ssize_t data_len = emu_buf_get_data_len(buf);
    if (data_len < 1) {
        slog_error_l("Data size is too short: %ld", data_len);
        return (0);
    }

    // MurmurHash3_x86_128_to_64(data, data_len, 0, &checksum);
    checksum = simple_checksum(data, data_len);
    //slog_note_l("Calculated the checksum: data len = %ld, data offset = %ld, checksum: %X clobal cnt = %d, ticket = %X",
    //            data_len, emu_buf_data_offset(buf), checksum, head->cnt_global, head->ticket);
    //
    // slog_error_l("Header checksums is wrong: calculated: %lX != received: %lX, global counter = %ld, ticket = %X",
    //			 checksum, head->checksum, head->cnt_global, head->ticket);
    return (checksum);
}

static int emu_calc_header_checksum(buf_t *buf)
{
    buf_head_t *head = emu_buf_get_head(buf);
    TESTP_ASSERT(head, "Can not get header");

    head->checksum = emu_calc_checksum(buf);

    if (0 == head->checksum) {
        slog_error_l("Wrong checksum 0; buf->used = %ld, buf->size = %ld; stopped",
                     buf->used, buf->room);
        abort();
    }

    // MurmurHash3_x86_32(buf_data_start, buf_data_len, 0, &checksum);

#if 0 /* SEB */ /* 26/10/2024 */
    for (size_t idx = start; idx < (size_t)buf->used; idx++) {
        // head->checksum ^= buf->data[idx];
        head->checksum += buf->data[idx];
    }
#endif /* SEB */ /* 26/10/2024 */
    return (0);
}

#if 0 /* SEB */ /* 12/11/2024 */
static size_t emu_calc_buf_data_start(pepa_core_t *core){
    size_t offset = 0;
    if (core->use_ticket) {
        offset += sizeof(pepa_ticket_t);
    }

    if (core->use_id) {
        offset += sizeof(pepa_id_t);
    }

    return (offset);
}
#endif /* SEB */ /* 12/11/2024 */

static int emu_check_header_checksum(buf_t *buf)
{
    buf_head_t *head = emu_buf_get_head(buf);
    TESTP(head, -1);

    emu_checksum_t checksum = emu_calc_checksum(buf);

    if (head->checksum == checksum) {
        return (0);
    }

    slog_error_l("Header checksums is wrong: calculated: %X != received: %X, global counter = %d, ticket = %X",
                 checksum, head->checksum, head->cnt_global, head->ticket);
    return (1);
}

static void emu_print_char_buf_hex(const char *buf, size_t buf_len)
{
    const size_t print_items = buf_len / sizeof(int);
    char buffer[1024];
    size_t offset_data = 0;
    size_t offset_pr = 0;
    unsigned int val;

    memset(buffer, 0, 256);

    for (size_t idx = 0; idx < print_items; idx++) {

        memcpy(&val, buf + offset_data, sizeof(unsigned int));
        offset_pr += sprintf(buffer + offset_pr, "%X ", val);
        offset_data += sizeof(unsigned int);
    }
    slog_note_l("Beginning of char *buf: |%s|", buffer);
}

static void emu_print_buf_begin(const buf_t *buf, int src)
{
    const size_t print_items = 16;
    char buffer[4096];
    size_t offset_data = 0;
    size_t offset_pr = 0;
    unsigned int val;

    memset(buffer, 0, 4096);

    if (BUF_SRC_IN == src) {
        offset_pr += sprintf(buffer + offset_pr, "SRC: IN | ");
    }

    if (BUF_SRC_SHVA == src) {
        offset_pr += sprintf(buffer + offset_pr, "SRC: SHVA | ");
    }

    for (size_t idx = 0; idx < print_items; idx++) {

        memcpy(&val, buf->data + offset_data, sizeof(unsigned int));
        offset_pr += sprintf(buffer + offset_pr, "%X ", val);
        offset_data += sizeof(unsigned int);
    }

    slog_note_l("Beginning of buf_t->data: [%s]", buffer);
}

pthread_mutex_t emu_buf_dump_lock = PTHREAD_MUTEX_INITIALIZER;

#define EMU_BUF_DUMP_DISABLE (0)

#if 1 /* SEB */ /* 12/11/2024 */
static void emu_buf_dump(buf_t *buf, const char *header)
{
    if (EMU_BUF_DUMP_DISABLE) {
        return;
    }

    emu_checksum_t checksum;

    pthread_mutex_lock(&emu_buf_dump_lock);
    // buf_head_t   *head     = BUF_HEAD(buf, 0);
    buf_head_t *head = emu_buf_get_head(buf);
    TESTP_VOID(head);
    unsigned int val = 0;

    char *src_name = "NA";

    if (head->src == BUF_SRC_IN) {
        src_name = "IN";
    }

    if (head->src == BUF_SRC_SHVA) {
        src_name = "SHVA";
    }

    checksum = emu_calc_checksum(buf);

    slog_note_l("=========================================================================");
    slog_note_l("%s", header);
    slog_note_l("HEADER SOURCE:   %s", src_name);
    slog_note_l("HEADER INSTANCE: %u", head->instance);
    slog_note_l("HEADER COUNTER:  %d", head->cnt);
    slog_note_l("GLOBAL COUNTER:  %d", head->cnt_global);
    slog_note_l("HEADER CHECKSUM: %X", head->checksum);
    slog_note_l("CALC.  CHECKSUM: %X", checksum);
    slog_note_l("HEADER LEN:      %u", head->len);
    slog_note_l("HEADER MARK:     %X", head->mark);
    slog_note_l("BUF_T  ROOM:     %ld", buf->room);
    slog_note_l("BUF_T  USED:     %ld", buf->used);
    slog_note_l("BUF_T  USED DUMP START:");
    slog_note_l(".........................................................................");

    for (size_t idx = 0; idx < (size_t)buf->used; idx += sizeof(val)) {
        val = 0;

        size_t copy_bytes = PEPA_MIN(sizeof(val), buf->used - idx);

        memcpy(&val, buf->data + idx, copy_bytes);
        printf("%X ", val);
    }

    printf("\n");
    slog_note_l("BUF_T  USED DUMP END:");
    slog_note_l(".........................................................................");

    slog_note_l("=========================================================================");

    emu_compare_sent_and_received_buffers(emu, buf);
    pthread_mutex_unlock(&emu_buf_dump_lock);
}
#endif /* SEB */ /* 12/11/2024 */

static emu_cnt_t emu_get_recv_cnt(int source, int instance)
{
    emu_cnt_t cnt = -1;
    if (BUF_SRC_IN == source) {
        cnt = emu->cnt_in_recv[instance];
    }

    if (BUF_SRC_SHVA == source) {
        cnt = emu->cnt_shva_recv;
    }

    //slog_note_l("GET RECV counter %d for %s", cnt, src_num_to_str(source, instance));

    return (cnt);
}

static void emu_set_recv_cnt(int source, int instance, emu_cnt_t cnt)
{
    if (BUF_SRC_IN == source) {
        emu->cnt_in_recv[instance] = cnt;
    }

    if (BUF_SRC_SHVA == source) {
        emu->cnt_shva_recv = cnt;
    }
    //slog_note_l("SET RECV counter %d for %s", cnt, src_num_to_str(source, instance));
}

static emu_cnt_t emu_get_send_cnt(int source, int instance)
{
    emu_cnt_t cnt;
    if (BUF_SRC_IN == source) {
        cnt = emu->cnt_in_sent[instance];
    }

    if (BUF_SRC_SHVA == source) {
        cnt = emu->cnt_shva_sent;
    }

    //slog_note_l("GET SENT counter %d for %s", cnt, src_num_to_str(source, instance));
    return (cnt);
}

static void emu_set_send_cnt(int source, int instance, emu_cnt_t cnt)
{
    if (BUF_SRC_IN == source) {
        emu->cnt_in_sent[instance] = cnt;
    }

    if (BUF_SRC_SHVA == source) {
        emu->cnt_shva_sent = cnt;
        //slog_note_l("SET SENT counter %d for %s", cnt, src_num_to_str(source, instance));
    }
}

static void emu_print_buffers_in_hex(const unsigned char *buf_sent, const unsigned char *buf_recv, const size_t buf_len)
{
    size_t num_uint32 = buf_len / sizeof(uint32_t);
    size_t remaining_bytes = buf_len % sizeof(uint32_t);

    // Print buffer as uint32_t values in hexadecimal
    for (size_t index = 0; index < num_uint32; ++index) {
        uint32_t value_sent = 0;
        uint32_t value_recv = 0;

        memcpy(&value_sent, buf_sent + index * sizeof(uint32_t), sizeof(uint32_t));
        memcpy(&value_recv, buf_recv + index * sizeof(uint32_t), sizeof(uint32_t));

        if (value_sent == value_recv) {
            printf("%08X ", value_sent);
        } else {
            printf("[%08x/%08x/] ", value_sent, value_recv);
            //printf("[SENT = %X != RECV = %X | offset = %lu / %lu] ", value_sent, value_recv, index * sizeof(uint32_t), buf_len);
            //printf("\n\n");
            // return;
        }

        if ((index + 1) % 12 == 0) {  // Newline every 4 values for readability
            printf("\n");
        }
    }

    // Print any remaining bytes as hexadecimal characters
    if (remaining_bytes > 0) {

        printf("\nRemaining bytes:\n");
        for (size_t index = 0; index < remaining_bytes; ++index) {
            size_t offset = num_uint32 * sizeof(uint32_t) + index;
            unsigned char value_sent = buf_sent[offset];
            unsigned char value_recv = buf_recv[offset];

            if (value_sent == value_recv) {
                printf("%08X ", value_sent);
            } else {
                printf("[SENT = %02X != RECV = %02X | offset = %zu / %zu] ", value_sent, value_recv, offset, buf_len);
            }

            if ((index + 1) % (12 * 3) == 0) {
                printf("\n\n");
                printf("\n");
            }

            //printf("%02X ", (unsigned char)buf_sent[num_uint32 * sizeof(uint32_t) + index]);
        }
    }
    printf("\n\n");
}

static void emu_compare_print_bufs(buf_t *buf_sent, buf_t *buf_recv, const char *header)
{
    // pepa_core_t  *core       = pepa_get_core();
    int do_not_dump = 0;

    //buf_head_t   *head_recv  = BUF_HEAD(buf_recv, 0);
    buf_head_t *head_recv = emu_buf_get_head(buf_recv);
    TESTP_VOID(head_recv);
    //buf_head_t   *head_sent  = BUF_HEAD(buf_sent, 0);
    buf_head_t *head_sent = emu_buf_get_head(buf_sent);
    TESTP_VOID(head_sent);

    slog_note_l("=========================================================================");
    slog_note_l("%s", header);

    if (head_recv->src == head_sent->src) {
        slog_note_l("HEADER SOURCE:   SAME: %u (%s)", head_sent->src, src_num_to_str(head_sent->src, head_recv->instance));
    } else {
        slog_note_l("HEADER SOURCE:   DIFFER: RECV = %u, SENT = %u", head_recv->src, head_sent->src);
    }

    if (head_recv->instance == head_sent->instance) {
        slog_note_l("HEADER INSTANCE: SAME: %u", head_recv->instance);
    } else {
        slog_note_l("HEADER INSTANCE: DIFFER: RECV = %u, SENT = %u", head_recv->instance, head_sent->instance);
    }

    if (head_recv->cnt == head_sent->cnt) {
        slog_note_l("HEADER COUNTER:  SAME: %d", head_recv->cnt);
    } else {
        slog_note_l("HEADER COUNTER:  DIFFER: RECV = %d, SENT = %d", head_recv->cnt, head_sent->cnt);
    }

    if (head_recv->cnt_global == head_sent->cnt_global) {
        slog_note_l("GLOBAL COUNTER:  SAME: %d", head_recv->cnt_global);
    } else {
        slog_note_l("GLOBAL COUNTER:  DIFFER: RECV = %d, SENT = %d", head_recv->cnt_global, head_sent->cnt_global);
    }

    if (head_recv->checksum == head_sent->checksum) {
        slog_note_l("HEADER CHECKSUM: SAME: %X", head_recv->checksum);
    } else {
        slog_note_l("HEADER CHECKSUM: DIFFER: RECV = %X, SENT = %X", head_recv->checksum, head_sent->checksum);
    }

    if (head_recv->len == head_sent->len) {
        slog_note_l("HEADER LEN:      SAME: %u", head_recv->len);
    } else {
        do_not_dump = 1;
        slog_note_l("HEADER LEN:      DIFFER: RECV = %u, SENT = %u", head_recv->len, head_sent->len);
    }

    if (head_recv->mark == head_sent->mark) {
        slog_note_l("HEADER MARK:     SAME: %X", head_recv->mark);
    } else {
        slog_note_l("HEADER MARK:     DIFFER: RECV = %X, SENT = %X", head_recv->mark, head_sent->mark);
    }

    // extra = emu_calc_buf_data_start(core);
    if (buf_sent->used == buf_recv->used) {
        slog_note_l("HEADER USED:     SAME: %ld", buf_recv->used);
    } else {
        do_not_dump = 1;
        slog_note_l("HEADER USED:     DIFFER: RECV = %ld, SENT = %ld", buf_recv->used, buf_sent->used);
    }

    if (do_not_dump) {
        return;
    }

    slog_note_l("BUF_T  USED DUMP START:");
    slog_note_l(".........................................................................");
    printf("\n");
    emu_print_buffers_in_hex((unsigned char *)buf_sent->data, (unsigned char *)buf_recv->data, (size_t)buf_sent->used);
    return;

    for (size_t idx = 0; buf_sent->used; idx++) {
        unsigned int val_sent;
        unsigned int val_recv;

        size_t copy_bytes = PEPA_MIN(sizeof(val_sent), buf_sent->used - idx);

        memcpy(&val_sent, buf_sent->data + idx, copy_bytes);
        memcpy(&val_recv, buf_recv->data + idx, copy_bytes);

        if (val_sent == val_recv) {
            printf("%X ", val_sent);
        } else {
            printf("[SENT = %X != RECV = %X | offset = %zu / %ld] ", val_sent, val_recv, idx, buf_sent->used);
            break;
        }

#if 0 /* SEB */ /* 02/11/2024 */
        if (data_sent[idx] == data_recv[idx]) {
            printf("%02X ", (unsigned int)data_sent[idx]);
        } else {
            printf("[offset in data = %lu | SENT = %02X != RECV = %02X] ",
                   idx, (unsigned int)data_sent[idx], (unsigned int)data_recv[idx]);
        }
#endif /* SEB */ /* 02/11/2024 */
    }

    printf("\n\n");
}

static void emu_compare_sent_and_received_buffers(emu_t *emu, buf_t *buf_recv)
{
    // buf_head_t *head_rx  = BUF_HEAD(buf_recv, 0);
    const buf_head_t *head_rx = emu_buf_get_head(buf_recv);
    TESTP_VOID(head_rx);

    buf_t *buf_sent = emu_get_buf_from_zhash(emu, head_rx->cnt_global);

    if (NULL == buf_sent) {
        slog_error_l("Can not find a saved buffer by ticket");
        return;
    }
    emu_compare_print_bufs(buf_sent, buf_recv, "BUFFER SENT != BUFFER RECEIVED");
    int rc = buf_free(buf_sent);
    if (rc) {
        slog_error_l("Can not free buf_t");
    }

    // slog_note_l("Compared sent and saved in zhash");
}

#if 0 /* SEB */ /* 02/11/2024 */
static void emu_print_buf_short(buf_t *buf, const char *caller){
    // buf_head_t *head = BUF_HEAD(buf, 0);
    buf_head_t *head = emu_buf_get_head(buf);
    TESTP_VOID(head);

    slog_note_l("[%s] [%s] BUF: cnt: %ld | len: %u | ticket: %X | chksum = %lX",
                caller, src_num_to_str(head->src, head->instance), head->cnt, head->len, head->ticket, head->checksum);
}
#endif /* SEB */ /* 02/11/2024 */

/* The buffer has a header, containg source type, source instance
 * for example, IN + 2, or SHVA + 0, and checksum.
 * Also, part of buffers may contain PEPA ID + Ticket (buffers that sourced from SHVA).
   We consider all this in this function */
static int emu_check_buffer(buf_t *buf, uint32_t expect_src, const char *caller)
{
    emu_cnt_t expect_cnt = 0;
    uint32_t source = 0;
    uint32_t instance = 0;

    // pepa_core_t *core      = pepa_get_core();
    buf_head_t *head = NULL;

    /* Get buffer header and check MARK */

    head = emu_buf_get_head(buf);

    TESTP_MES(head, -1, "Can not extract head");

    /* Test we got valid header */
    if (NULL == head) {
        // slog_error_l("[%s] Can not find head: expected %X but it is %X", caller, (unsigned int)BUF_HEADER_MARK, head->mark);
        slog_error_l("[%s] Can not find head", caller);
        emu_print_buf_begin(buf, expect_src);
        return (-1);
    }

    /* Test source */
    source = head->src;
    instance = head->instance;

    if (source <= BUF_SRC_START || source > BUF_SRC_FINISH) {
        slog_error_l("[%s] Bad source: expeted src [%d, %d], but it is %u",
                     caller, BUF_SRC_START + 1, BUF_SRC_FINISH - 1, source);
        return (-1);
    }

    if (source != expect_src) {
        slog_error_l("[%s] Wrong source: expected %u (%s) but it is %u (%s)",
                     caller, expect_src, src_num_to_str(expect_src, 0),
                     source, src_num_to_str(source, head->instance));
    }

    /* Test the counter */

    int last_received = emu_get_recv_cnt(source, instance);
    if (last_received < 0) {
        slog_error_l("Error: expected counter >= 0");
        abort();
    }

    // slog_note_l("last_received = %d", last_received);

    if (0 == last_received) {
        expect_cnt = head->cnt;
    } else {
        expect_cnt = emu_get_recv_cnt(source, instance) + 1;
    }

    emu_set_recv_cnt(source, instance, head->cnt);

    if (head->cnt > 0 && head->cnt != expect_cnt) {
        slog_error_l("[%s] Wrong counter: expected count: %d but it is: %d from %s | gobal cnt: %d | zhash amount = %zu | chksum = %X",
                     caller, expect_cnt, head->cnt, src_num_to_str(head->src, head->instance), head->cnt_global, emu->zhash_count, head->checksum);
        return -4;
    }

    /* Update the 'recv' counter */
    // emu_set_recv_cnt(source, instance, head->cnt);

    //slog_note_l("Set new counter: %d", head->cnt);

    /* Test received len vs expected */

    if (buf->used != head->len) {
        slog_error_l("[%s] Wrong length: expected %u but it is %u", caller, head->len, (unsigned int)buf->used);
        return (-5);
    }

    /* Test the buffer content checksum */

    if (0 != emu_check_header_checksum(buf)) {
        slog_error_l("[%s] Wrong checksum: buf_t len is %u, expected %u", caller, (unsigned int)buf->used, head->len);
        // emu_print_buf_begin(buf, expect_src);
        emu_compare_sent_and_received_buffers(emu, buf);
        return (-6);
    }

    /* Remove saved buffer */
    buf_t *buf_sent = emu_get_buf_from_zhash(emu, head->ticket);
    if (buf_sent) {
        int rc = buf_free(buf_sent);
        if (rc) {
            slog_error_l("Can not free buf_t");
        }
    }
    return (0);
}

/**
 * @author Sebastian Mountaniol (27/10/2024)
 * @brief Fill the whole buffer with a pattern
 * @param buf_t * buf Buffer to fill with the pattern
 * @param uint32_t pattern Pattern to use
 * @details
 */
static void emu_buf_fill_pattern(buf_t *buf, uint32_t pattern)
{
    size_t i;
    uint32_t *buf_32 = (uint32_t *)buf->data;

    // Fill the buffer in 32-bit chunks using the pattern
    for (i = 0; i < buf->room / sizeof(uint32_t); i++) {
        buf_32[i] = pattern;
    }

    // Handle any remaining bytes if buf_size is not a multiple of 4
    char *remaining_bytes = (char *)(buf_32 + i);
    for (size_t j = 0; j < buf->room % sizeof(uint32_t); j++) {
        remaining_bytes[j] = ((char *)&pattern)[j];
    }
}

/**
 * @author Sebastian Mountaniol (27/10/2024)
 * @brief Construct binary pattern from 'src' and 'instance'
 * @param uint32_t src
 * @param uint32_t instance
 * @return uint32_t
 * @details
 */
static uint32_t emu_construct_pattern(uint32_t src, uint32_t instance)
{
    if (src == BUF_SRC_SHVA) {
        return SHVA_BUF_PATTERN;
    }

    if (src == BUF_SRC_IN) {
        return IN_BUF_PATTERN + instance;
    }

    abort();


    //return (((src & 0xFFFF) << 16) | (instance & 0xFFFF));
}
__attribute__((unused))
static int emu_check_buffer_pattern(const buf_t *buf, uint32_t src, uint32_t instance)
{
    uint32_t pattern = emu_construct_pattern(src, instance);
    const uint32_t *data_32 = (const uint32_t *)buf->data;
    size_t num_elements = buf->used / sizeof(uint32_t);

    // Check 32-bit chunks
    for (size_t i = 0; i < num_elements; i++) {
        if (data_32[i] != pattern) {
            printf("The pattern is broken in offset %zu\n", i * sizeof(uint32_t));
            return (i * sizeof(uint32_t));
        }
    }

    // Check remaining bytes if used is not a multiple of 4
    const char *remaining_bytes = (const char *)(data_32 + num_elements);
    for (size_t j = 0; j < buf->used % sizeof(uint32_t); j++) {
        if (remaining_bytes[j] != ((char *)&pattern)[j]) {
            printf("The pattern is broken in offset %zu\n", num_elements * sizeof(uint32_t) + j);
            return (num_elements * sizeof(uint32_t) + j);
        }
    }

    return (0);  // Buffer is filled correctly
}

static int32_t pepa_emulator_generate_buffer_buf(buf_t *buf, const size_t buffer_size, const uint64_t cnt, const uint32_t src, const uint32_t instance)
{
    uint32_t pattern = emu_construct_pattern(src, instance);

    TESTP(buf, -1);
    buf->used = 0;

    // const size_t head_size     = sizeof(buf_head_t);

    const size_t required_room = buffer_size; // + head_size;

    /* If not enough room in the buf_t, increase it */
    if (buf->room < (buf_s64_t)required_room) {
        ret_t rc_buf = buf_test_room(buf, (buf_s64_t)required_room);

        if (BUFT_OK != rc_buf) {
            slog_fatal_l("Could not allocate boof room: %d: %s", rc_buf, buf_error_code_to_string((int)rc_buf));
            abort();
        }
    }

    emu_buf_fill_pattern(buf, pattern);

    /* Add buf header to the buff */
    emu_set_header_buffer(buf, cnt, src, instance);

    emu_set_header_len(buf, required_room);
    buf->used = required_room;
    emu_calc_header_checksum(buf);
    // emu_check_buffer(buf, src, 1);
    return (PEPA_ERR_OK);
}

/**
 * @author Sebastian Mountaniol (30/10/2024)
 * @brief Test received header, find either it valid or not
 * @param buf_head_t * head Pointer to the header
 * @return int32_t 0 if it is valid, a negative otherwise
 * @details
 */
static int32_t emu_test_header(const buf_head_t *head, const char *name)
{
    int ret = 0;
    const size_t head_size = sizeof(buf_head_t);
    TESTP_ASSERT(head, "Pointer to the header is NULL");

    if (0 == head->checksum) {
        slog_error_l("[%s] Buffer header checksum is 0, it is illigal situation", name);
        ret--;
    }

    if (BUF_HEADER_START != head->start) {
        slog_error_l("[%s] Buffer header start is invalid: %X, expected %X", name, head->start, BUF_HEADER_START);
        ret--;
    }

    if (BUF_HEADER_MARK != head->mark) {
        slog_error_l("[%s] Buffer header mark is invalid: %X, expected %X", name, head->mark, (unsigned int)BUF_HEADER_MARK);
        ret--;
    }

    if (0 == head->ticket) {
        slog_error_l("[%s] Buffer header ticket is invalid: %X, expected not 0", name, head->ticket);
        ret--;
    }

    if (head->len <= head_size) {
        slog_error_l("[%s] Buffer len in the header is too short: %u, expected > %lu", name, head->len, head_size);
        ret--;
    }

    if (head->src <= BUF_SRC_START || head->src >= BUF_SRC_FINISH) {
        slog_error_l("[%s] Buffer src in the header is too wrong: %u, expected > %d and < %d",
                     name, head->src, BUF_SRC_START, BUF_SRC_FINISH);
        ret--;
    }
    return (ret);
}

#if 0 /* SEB */ /* 15/11/2024 */
static void print_hex_32(const char *buffer, size_t size){
    // Ensure the buffer has enough bytes to interpret as uint32_t values
    size_t num_elements = size / sizeof(uint32_t);

    for (size_t i = 0; i < num_elements; ++i) {
        uint32_t value;
        // Copy 4 bytes from buffer to value
        memcpy(&value, buffer + i * sizeof(uint32_t), sizeof(uint32_t));
        printf("%08X ", value);

        if ((i + 1) % 4 == 0) {  // Newline every 4 values (16 bytes) for readability
            printf("\n");
        }
    }

    // Newline if we didn't end on a complete line
    if (num_elements % 4 != 0) {
        printf("\n");
    }
}
#endif /* SEB */ /* 15/11/2024 */
#if 0 /* SEB */ /* 02/11/2024 */

static void emu_finish_read_head(buf_t *buf){
    size_t read_more = sizeof()
}
#endif /* SEB */ /* 02/11/2024 */

static int emu_read_one_buffer(buf_t *buf,
                               const int fd,
                               size_t *rx,
                               size_t *reads,
                               const int src_id,
                               const char *name)
{
    static ssize_t cnt = 0;

    TESTP_ASSERT(buf, "buf is NULL");
    TESTP_ASSERT(rx, "rx is NULL");
    TESTP_ASSERT(reads, "reads is NULL");
    TESTP_ASSERT(name, "name is NULL");

    pepa_core_t *core = pepa_get_core();
    int rc;
    buf->used = 0;
    const size_t head_size = sizeof(buf_head_t);
    buf_head_t *head = NULL;

    cnt++;

    /*** BEGIN BUF_HEADER ***/

    rc = buf_test_room(buf, head_size * 2);
    if (rc) {
        slog_fatal_l("[%s][%ld] buf_test_room failed: %d, %s", name, cnt, rc, buf_error_code_to_string(rc));
        abort();
    }

    rc = buf_fill_with_zeros(buf);
    if (rc) {
        slog_error_l("Can not fill buf_t with zeros");
        abort();
    }

    /* Read from socket */

    /* Receive header only */
    rc = recv_exact(fd, (char *)buf->data, buf->room, head_size, __func__, __LINE__);

    if (rc != (int)(head_size)) {
        slog_error_l("[%s][%ld] Could not receive the header: rc = %d", name, cnt, rc);
        return (-1);
    }
    buf->used += rc;

    // slog_note_l("[%s][%ld] Going to extract head", name, cnt);
    head = BUF_HEAD(buf, 0);

    // slog_note_l("[%s][%ld] buffer data is %p, head is %p, diff is %ld", name, cnt, buf->data, head, ((char *)head - buf->data));

    //if (head->mark != BUF_HEADER_MARK) {
    if (0 != emu_test_header(head, name)) {
        slog_error_l("[%s][%ld] Received a wrong header", name, cnt);
        emu_print_buf_begin(buf, src_id);
        //slog_error_l("[%s][%ld] HEAD DUMP", name, cnt);
        //print_hex_32((char *)head, sizeof(head));
        //slog_error_l("[%s][%ld] //HEAD DUMP", name, cnt);

        return (-1);
    }

    // buf->used += head_size;
    *rx += rc;
    *reads += 1;

    /*** END BUF_HEADER ***/


    /*** BEGIN BUFFER BODY ***/
    /* Receive the rest of the buffer */

    const size_t rest = head->len - head_size;

    if (rest + buf->used > core->emu_max_buf) {
        slog_error_l("[%s][%ld] Bad buffer length: rest (%lu) > core->emu_max_buf (%lu)", name, cnt, rest + buf->used, core->emu_max_buf);
        abort();
    }

    // emu_calc_buf_data_start

    rc = buf_test_room(buf, rest);
    if (rc) {
        slog_fatal_l("[%s][%ld] buf_test_room failed: %d, %s", name, cnt, rc, buf_error_code_to_string(rc));
        abort();
    }

    // buf_fill_with_zeros(buf);

    rc = recv_exact(fd, buf->data + buf->used, buf->room - buf->used, rest, __func__, __LINE__);

    if (rc != (int)rest) {
        slog_fatal_l("[%s][%ld] Can not receive the rest: asked %lu, received %d", name, cnt, rest, rc);
        abort();
        return (-1);
    }

    buf->used += rc;

    // emu_buf_dump(buf,  name);

    if (0 != emu_check_buffer(buf, src_id, name)) {
        slog_error_l("[%s][%ld] BAD BUFFER", name, cnt);
        char err_string[64] = {0};
        sprintf(err_string, "[%s][%ld] RECEIVED A BAD BUFFER", name, cnt);
        emu_buf_dump(buf, err_string);
        //abort();
        //return (-1);
        return buf->used;
    }

    *rx += buf->used;
    // slog_note_l("[%s][%ld] Everything is all right, returning: %ld", name, cnt, buf->used);
    return (buf->used);
}

static int emu_gen_and_send(const int fd,
                            size_t *tx,
                            size_t *writes,
                            const int iterations,
                            const int buf_src,
                            const int instance,
                            const char *name)
{
    buf_t *buf = NULL;
    int cur = 0;
    ssize_t rc;
    pepa_core_t *core = pepa_get_core();

    for (int i = 0; i < iterations; i++) {
        *writes += 1;
        if (FD_CLOSED == fd) {
            return (0);
        }

        /// size_t      buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
        size_t buf_size = emu_random_buf_size();
        buf = buf_new(buf_size);
        if (NULL == buf) {
            slog_error_l("[%s] Can not allocate buf_t of size %lu; aborting", name, buf_size);
            abort();
        }

        rc = buf_fill_with_zeros(buf);
        if (0 != rc) {
            slog_error_l("[%s] Can not fill zeroes the buf_t of size %lu; aborting", name, buf_size);
            abort();
        }

        /* Get the last 'sent' counter*/
        emu_cnt_t buf_count = emu_get_send_cnt(buf_src, instance);

        if (PEPA_ERR_OK != pepa_emulator_generate_buffer_buf(buf, /* Existing buffer */
                                                             buf_size, /* The buffer size we want to generate */
                                                             buf_count, /* Counter */
                                                             buf_src, /* Source == IN */
                                                             instance)/* IN Instance number */) {
            slog_error_l("[%s] Can not generate buffer", name);
            abort();
        }

        /* Test the buffer before send it */
#if 0 /* SEB */ /* 13/11/2024 */
        rc = emu_check_buffer(buf, buf_src, "TEST BEFORE SEND");
        if (rc) {
            slog_fatal_l("The generated buffer is wrong");
            abort();
        }
#endif /* SEB */ /* 13/11/2024 */

        const buf_head_t *head = BUF_HEAD(buf, 0);
        if (0 == head->checksum) {
            slog_error_l("On send: checksum == 0");
            abort();
        }

        // rc = send_exact(fd, buf->data, buf->used, name, instance);
        // rc = send_exact(fd, buf->data, buf->used, name);
        rc = send_exact(fd, buf->data, buf->used);
        if (rc != buf->used) {
            slog_warn_l("[%s] Could not sent buffer", name);
            rc = buf_free(buf);
            if (rc) {
                slog_error_l("Can not free buf_t");
            }
            return (-1);
        }

        /* Bfffer is sent; save it to the zhash, when it received from another socket, we want to compare */
        emu_save_buf_to_zhash(emu, buf);

        /* Increase the 'sent' counter */
        buf_count++;
        emu_set_send_cnt(buf_src, instance, buf_count);

        *tx += (uint64_t)rc;

        if (*writes > 0 && 0 == (*writes % RX_TX_PRINT_DIVIDER)) {
            slog_debug_l("[%s] %-7lu reads, bytes: %-7lu, Kb: %-7lu", name, *writes, *tx, (*tx / 1024));
        }

        if (core->emu_timeout > 0) {
            usleep(core->emu_timeout);
        }

        // slog_note_l("[%s] Send buffer, len: %ld", name, buf->used);

        //if (buf_src == BUF_SRC_SHVA) {
        //    emu_buf_dump(buf,  "SHA SENT");
        // }
    }
    return (cur);
}


static int32_t out_start_connection(void)
{
    int sock;
    pepa_core_t *core = pepa_get_core();

    do {
        sock = pepa_open_connection_to_server(core->out_thread.ip_string->data, core->out_thread.port_int, __func__);
        if (sock < 0) {
            slog_error_l("Emu OUT: Could not connect to OUT (returned %d); |%s| ; waiting...", sock, strerror(errno));
            sleep(5);
        }
    } while (sock < 0);

    /* Set socket properties */
    pepa_set_tcp_timeout(sock);
    pepa_set_tcp_recv_size(core, sock, "OUT");

    slog_debug_l("Established connection to OUT: %d", sock);
    return (sock);
}

/**************** THREADS ***************************/

typedef struct {
    int epoll_fd;
    emu_t *emu;
} out_thread_args_t;

static void pepa_emulator_out_thread_cleanup(__attribute__((unused))void *arg)
{
    // int         *event_fd = (int *)arg;
    out_thread_args_t *args = arg;
    pepa_core_t *core = pepa_get_core();
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    OUT CLEANUP                   $$$$$$$$$");

    int rc_remove = epoll_ctl(args->epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);

    if (rc_remove) {
        slog_warn_l("[OUT] Could not remove RW socket (FD = %d) from epoll set %d", core->sockets.out_write, args->epoll_fd);
    }

    pepa_reading_socket_close(core->sockets.out_write, "EMU OUT");

    /* Update status */
    emu_set_out_status(args->emu, ST_NO);

    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

/* Internal thread: reads from the pipe packets with removet prebuf structure */
#if 1 /* SEB */ /* 15/11/2024 */
static void *pepa_emu_out_internal_thread(__attribute__((unused))void *arg)
{
    const char *my_name = "OUT INT";
    emu_t *emup = arg;
    TESTP_MES(emup, NULL, "Thread argument is the NULL pointer");

    /* Update the status */
    // emu_set_out_status(emu, ST_STARTING);

    // int         epoll_fd;
    pepa_core_t *core = emup->core;
    uint64_t reads = 0;
    uint64_t rx = 0;

    pthread_setname_np(pthread_self(), "OUT-INT");
    pthread_block_signals(my_name);

    /* In this thread we read from socket as fast as we can */

    slog_note_l("[%s] Going to allocate a new buf_t, size: %lu", my_name, core->emu_max_buf + 1);
    buf_t *buf = buf_new((buf_s64_t)core->emu_max_buf + 1);


    /* Entering the running loop */
    do {
        /* Read from socket */
        ssize_t rc = emu_read_one_buffer(buf, pipe_out[PEPA_OUT_R], &rx, &reads, BUF_SRC_SHVA, my_name);
        // rc = read(pipe_out[PEIPE_OUT_R], buf, core->emu_max_buf);
        if (rc < 0) {
            slog_error_l("[%s]: Read/Write op between sockets failure: %s", my_name, strerror(errno));
            abort();
        }

        emup->bytes_rx_out_internal += rc;

        // slog_trace_l("[%s]: Received bytes: %ld", my_name, rc);
        rx += (uint64_t)rc;

    } while (1); /* epoll loop */

    /* Now we can start send and recv */
    pthread_exit(NULL);
}
#endif /* SEB */ /* 15/11/2024 */

static int pepa_emulator_out_read_buf(const int fd, size_t *rx, size_t *reads)
{
    // int errors = 0;
    int cur = 0;
    int rc = -5;

    //for (size_t idx = 0; idx < 5; idx++) {
    *reads = *reads + 1;
    buf_t *buf = buf_new(0);
    rc = emu_read_one_buffer(buf, fd, rx, reads, BUF_SRC_SHVA, "OUT READ");
    emu->num_shva_reads++;

    if (rc < 1) {
        const buf_head_t *head = emu_buf_get_head(buf);
        emu_cnt_t buf_count = emu_get_recv_cnt(head->src, head->instance);
        slog_error_l("Could not receive the header; returned %d; previous cnt = %d", rc, buf_count);
        return (cur);
    }

    cur += rc;

    rc = buf_free(buf);
    if (rc) {
        slog_error_l("Can not free buf_t");
    }

    return (cur);
}

/**
 * @author Sebastian Mountaniol (09/11/2024)
 * @briefRead packets from the PEPA, remove ticket + pepa ID + counter, pass to the next thread through pipe
 *    them
 * @param void * arg
 * @return void*
 * @details
 */
static void *pepa_out_external_thread(void *arg)
{
    // pthread_t internal_thread_id;
    const char *my_name = "OUT EXT";
    emu_t *emup = arg;
    TESTP_MES(emup, NULL, "Thread argument is the NULL pointer");

    out_thread_args_t args;
    args.emu = emup;

    /* Update the status */
    emu_set_out_status(emup, ST_STARTING);

    // int         epoll_fd;
    pthread_cleanup_push(pepa_emulator_out_thread_cleanup, (void *)&args);
    ssize_t rc = -1;
    pepa_core_t *core = emup->core;
    int32_t event_count;
    int32_t i;

    //uint64_t reads = 0;
    // uint64_t rx = 0;

    char *buf = malloc(core->emu_max_buf);
    TESTP_ASSERT(buf, "Can not allocate buffer");

    pthread_setname_np(pthread_self(), "OUT-EXT");
    pthread_block_signals(my_name);

    // core->sockets.out_write = out_start_connection();

    do {
        core->sockets.out_write = out_start_connection();
        if (core->sockets.out_write < 0) {
            sleep(1);
            continue;
        }

        slog_note_l("[%s] Opened out socket: fd: %d, port: %d",
                    my_name, core->sockets.out_write, pepa_find_socket_port(core->sockets.out_write));

        struct epoll_event events[1];
        args.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (args.epoll_fd < 0) {
            slog_fatal_l("[%s] Can not create eventfd: %s", my_name, strerror(errno));
            abort();
        }

        if (0 != epoll_ctl_add(args.epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            slog_warn_l("[%s]: Tried to add sock (FD = %d) and failed", my_name, core->sockets.out_write);
            goto closeit;
        }

        /* Update the status */
        emu_set_out_status(emup, ST_WAITING);

        /* Entering the running loop */
        do {

            int status = emu_get_out_status(emup);

            switch (status) {
                case ST_WAITING:
                    usleep(10000);
                    continue;
                case ST_STOPPED:
                    goto stop_out_thread;
                case ST_RESET:
                    goto closeit;
                default:
                    break;
            }

            //pthread_create(&internal_thread_id, NULL, pepa_emu_out_internal_thread, arg);

            event_count = epoll_wait(args.epoll_fd, events, 1, 1000);

            const int err = errno;
            /* Nothing to do, exited by timeout */
            if (0 == event_count) {
                continue;
            }

            /* Interrupted by a signal */
            if (event_count < 0 && EINTR == err) {
                continue;
            }

            if (event_count < 0) {
                slog_warn_l("    OUT: error on wait: %s", strerror(err));
                goto closeit;
            }

            for (i = 0; i < event_count; i++) {

                const int fd = events[i].data.fd;
                const int evs = events[i].events;

                if (evs & EPOLLRDHUP) {
                    slog_warn_l("[%s]: The remote disconnected: %s", my_name, strerror(err));
                    goto closeit;
                }

                if (evs & EPOLLHUP) {
                    slog_warn_l("[%s]: Hung up detected: %s", my_name, strerror(err));
                    goto closeit;
                }

                if (!(evs & EPOLLIN)) {
                    continue;
                }


                /* Read from socket */
                //if (evs & EPOLLIN) {
                //do {
                /* Very unlikely we get in here */
                if (fd != core->sockets.out_write) {
                    slog_fatal_l("[%s] Received a epoll event but the event fd (%d) !=  out_write (%d)",
                                 my_name, fd, core->sockets.out_write);
                    abort();
                }

                pepa_prebuf_t prebuf;
                /* Receive prebuffer */
                rc = recv_exact(core->sockets.out_write, (char *)&prebuf, sizeof(pepa_prebuf_t), sizeof(pepa_prebuf_t), __func__, __LINE__);

                if (rc < 0) {
                    slog_error_l("[%s]: Can not read prebuf: on socket: %d", my_name, bytes_available_read(core->sockets.out_write));
                    goto closeit;
                }
                // emu->bytes_rx_out_external += rc;
                emup->bytes_rx_out_external += sizeof(pepa_prebuf_t);

                // slog_note_l("[%s]: Received bytes: %ld", my_name, rc);

                if (prebuf.pepa_id != core->id_val) {
                    slog_error_l("[%s] Received a wrong prebuffer: the PEPA ID: expected %X, but it is %X",
                                 my_name, core->id_val, prebuf.pepa_id);
                    emu_print_char_buf_hex((char *)&prebuf, sizeof(pepa_prebuf_t));
                    abort();
                }

                // slog_note_l("[%s]: Prebuf is OK", my_name);

                /* Recevie main buffer */


                //slog_note_l("BEFORE READING BUF");
#if 0 /* SEB */ /* 16/11/2024 */
                rc = pepa_emulator_out_read_buf(core->sockets.out_write,
                                                (size_t *)&emup->bytes_rx_out_external,
                                                (size_t *)&emu->num_out_reads);

                //slog_note_l("AFTER  READING BUF");
                if (rc < 1) {
                    slog_error_l("[%s] Can not read from OU a buffer", my_name);
                    goto closeit;
                }

                // emu->num_out_reads++;
                continue;
#endif /* SEB */ /* 16/11/2024 */
#if 1 /* SEB */ /* 15/11/2024 */

                memset(buf, 8, core->emu_max_buf);
                rc = recv_exact(core->sockets.out_write, buf, prebuf.pepa_len, prebuf.pepa_len, __func__, __LINE__);

                if (rc < 0) {
                    slog_error_l("[%s]: Can not read buffer; expected len is: %u", my_name, prebuf.pepa_len);
                    goto closeit;
                }

                emu->num_out_reads++;

                emup->bytes_rx_out_external += rc;


                // slog_note_l("[%s]: Received bytes: %ld", my_name, rc);

                /* Write the buffer to the pipe */

                //slog_note_l("BEFORE WRITING TO OIN-INT");
                rc = write(pipe_out[PEPA_OUT_W], buf, prebuf.pepa_len);
                //slog_note_l("AFTER  WRITING TO OIN-INT");

                if (rc < 0) {
                    slog_error_l("[%s]: Can not send buffer to the pipe", my_name);
                    goto closeit;
                }

                emu->num_out_writes++;

                emup->bytes_tx_out_external += rc;

                // slog_note_l("[%s]: Send to internal OUT INT bytes: %ld", my_name, rc);

                emup->bytes_rx_out_external += (uint64_t)rc;
#if 0 /* SEB */ /* 16/11/2024 */

                if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
                    slog_debug_l("[%s]: %-7lu reads, bytes: %-7lu, Kb: %-7lu",
                                 my_name, reads, rx, (rx / 1024));
                }

#endif /* SEB */ /* 16/11/2024 */
                // reads++;
                emu->num_out_reads++;
#endif /* SEB */ /* 15/11/2024 */
                //} while (rc > 0);

                //} /* for (i = 0; i < event_count; i++) */
            } /* for (i = 0; i < event_count; i++)  */

            /* Sometimes emulator should brake the connection: break the loop, then the socket will be closed */
            if (SHVA_SHOULD_EMULATE_DISCONNECT()) {
                slog_note_l("OUT      : EMULATING DISCONNECT");
                pepa_emulator_disconnect_mes("OUT");
                goto closeit;
            }

        } while (1); /* epoll loop */
    closeit:

        //pthread_cancel(internal_thread_id);
        emu_set_out_status(emup, ST_WAITING);

        rc = epoll_ctl(args.epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);

        if (rc) {
            slog_warn_l("[OUT]  Could not remove RW socket (FD = %d) from epoll set %d", core->sockets.out_write, args.epoll_fd);
        }

        close(args.epoll_fd);
        // pepa_reading_socket_close(core->sockets.out_write, "OUT RW");
        pepa_socket_close(core->sockets.out_write, "OUT RW");
        // emu_set_recv_cnt(BUF_SRC_SHVA, 0, 0);

        sleep(3);
    } while (1);
    /* Now we can start send and recv */
stop_out_thread:
    free(buf);
    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}



/* Create 1 read socket to emulate OUT connection */
#if 0  /* SEB */ /* 10/11/2024 */
static void *pepa_emulator_out_thread(__attribute__((unused))void *arg){
    emu_t *emu = arg;
    TESTP_MES(emu, NULL, "Thread argument is the NULL pointer");

    out_thread_args_t args;
    args.emu = emu;

    /* Update the status */
    emu_set_out_status(emu, ST_STARTING);

    // int         epoll_fd;
    pthread_cleanup_push(pepa_emulator_out_thread_cleanup, (void *)&args);
    ssize_t rc = -1;
    pepa_core_t *core = emu->core;
    int32_t event_count;
    int32_t i;

    uint64_t reads = 0;
    uint64_t rx = 0;

    pthread_block_signals("OUT");

    /* In this thread we read from socket as fast as we can */

    slog_note_l("[OUT] Going to allocate a new buf_t, size: %lu", core->emu_max_buf + 1);
    buf_t *buf = buf_new((buf_s64_t)core->emu_max_buf + 1);

    do {
        core->sockets.out_write = out_start_connection();
        if (core->sockets.out_write < 0) {
            sleep(1);
            continue;
        }

        slog_note_l("[OUT] Opened out socket: fd: %d, port: %d", core->sockets.out_write, pepa_find_socket_port(core->sockets.out_write));

        struct epoll_event events[2];
        args.epoll_fd = epoll_create1(EPOLL_CLOEXEC);

        if (0 != epoll_ctl_add(args.epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            slog_warn_l("    OUT: Tried to add sock fd = %d and failed", core->sockets.out_write);
            goto closeit;
        }

        /* Update the status */
        emu_set_out_status(emu, ST_WAITING);

        /* Entering the running loop */
        do {

            int status = emu_get_out_status(emu);

            switch (status) {
                case ST_WAITING:
                usleep(10000);
                continue;
                case ST_STOPPED:
                goto stop_out_thread;
                case ST_RESET:
                goto closeit;
                default:
                break;
            }

            event_count = epoll_wait(args.epoll_fd, events, 1, 300000);
            int err = errno;
            /* Nothing to do, exited by timeout */
            if (0 == event_count) {
                continue;
            }

            /* Interrupted by a signal */
            if (event_count < 0 && EINTR == err) {
                continue;
            }

            if (event_count < 0) {
                slog_warn_l("    OUT: error on wait: %s", strerror(err));
                goto closeit;
            }

            for (i = 0; i < event_count; i++) {

                if (events[i].events & EPOLLRDHUP) {
                    slog_warn_l("    OUT: The remote disconnected: %s", strerror(err));
                    goto closeit;
                }

                if (events[i].events & EPOLLHUP) {
                    slog_warn_l("    OUT: Hung up happened: %s", strerror(err));
                    goto closeit;
                }

                /* Read from socket */
                if (events[i].events & EPOLLIN) {
                    do {
                        rc = emu_read_one_buffer(buf, core->sockets.out_write, &rx, &reads, BUF_SRC_SHVA, "OUT");
                        // rc = read(core->sockets.out_write, buf->data, core->emu_max_buf);
                        if (rc < 0) {
                            slog_warn_l("    OUT: Read/Write op between sockets failure: %s", strerror(errno));
                            goto closeit;
                        }
                        rx += (uint64_t)rc;

                        if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
                            slog_debug_l("     OUT: %-7lu reads, bytes: %-7lu, Kb: %-7lu", reads, rx, (rx / 1024));
                        }

                    } while (rc > 0);

                    continue;
                } /* for (i = 0; i < event_count; i++) */
            }

            /* Sometimes emulate broken connection: break the loop, then the socket will be closed */
            if (SHVA_SHOULD_EMULATE_DISCONNECT()) {
                slog_note_l("OUT      : EMULATING DISCONNECT");
                pepa_emulator_disconnect_mes("OUT");
                goto closeit;
            }

        } while (1); /* epoll loop */
        closeit:
        emu_set_out_status(emu, ST_WAITING);

        rc = epoll_ctl(args.epoll_fd, EPOLL_CTL_DEL, core->sockets.out_write, NULL);

        if (rc) {
            slog_warn_l("[OUT]  Could not remove RW socket (FD = %d) from epoll set %d", core->sockets.out_write, args.epoll_fd);
        }

        close(args.epoll_fd);
        // pepa_reading_socket_close(core->sockets.out_write, "OUT RW");
        pepa_socket_close(core->sockets.out_write, "OUT RW");
        sleep(3);
    } while (1);
    /* Now we can start send and recv */
    stop_out_thread:
    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}
#endif /* SEB */ /* 10/11/2024 */

typedef struct {
    pthread_t shva_read_t;
    pthread_t shva_write_t;
} shva_args_t;

typedef struct {
    buf_t *buf;
    int eventfd;
    int sock_listen;
    emu_t *emu;
} shva_rw_thread_clean_t;


/** SHVA ***/

#define EVENTS_NUM (1)

static int pepa_emulator_shva_read(const int fd, size_t *rx, size_t *reads)
{
    // int errors = 0;
    int cur = 0;
    int rc = -5;

    //for (size_t idx = 0; idx < 5; idx++) {
    *reads += 1;
    buf_t *buf = buf_new(0);
    rc = emu_read_one_buffer(buf, fd, rx, reads, BUF_SRC_IN, "SHVA READ");
    emu->num_shva_reads++;

    if (rc < 1) {
        const buf_head_t *head = emu_buf_get_head(buf);
        emu_cnt_t buf_count = emu_get_recv_cnt(head->src, head->instance);
        slog_error_l("Could not receive the header; returned %d; previous cnt = %d", rc, buf_count);
        return (cur);
    }

    cur += rc;

#if 0 /* SEB */ /* 13/11/2024 */
    rc = emu_check_buffer(buf, BUF_SRC_IN, "SHVA");
    if (0 != rc) {
        slog_error_l("BAD BUFFER");
        slog_warn_l("Could not receive buffer payload");
        rc = buf_free(buf);
        if (rc) {
            slog_error_l("Can not free buf_t");
        }
        abort();
        return (-5);
    }
#endif /* SEB */ /* 13/11/2024 */

    rc = buf_free(buf);
    if (rc) {
        slog_error_l("Can not free buf_t");
    }

    return (cur);
}

static int pepa_emulator_shva_write(const int fd, size_t *tx, size_t *writes)
{
    return (emu_gen_and_send(fd, tx, writes, 5, BUF_SRC_SHVA, 0, "SHVA"));
}

static int pepa_emu_shva_accept(int epoll_fd, int sock_listen, pepa_core_t *core)
{
    struct sockaddr_in s_addr;
    socklen_t addrlen = sizeof(struct sockaddr);

    // slog_warn_l("[SHVA] Listening socket: got connection");

    /* If we have a RW socket opened, but there is another incoming connection is already established, we ignore this one */
    if (FD_CLOSED != core->sockets.shva_rw) {
        // slog_error_l("[SHVA] We have RW socket but another connection is detected: we ignore it");
        return (-1);
    }

    /* All right, we accept the connection */

    core->sockets.shva_rw = accept(sock_listen, &s_addr, &addrlen);

    slog_note_l("[SHVA] ACCEPTED (FD = %d)", core->sockets.shva_rw);
    if (core->sockets.shva_rw < 0) {
        slog_error_l("[SHVA] Could not accept: %s", strerror(errno));
        core->sockets.shva_rw = FD_CLOSED;
        return (-1);
    }

    pepa_set_tcp_timeout(core->sockets.shva_rw);
    pepa_set_tcp_send_size(core, core->sockets.shva_rw, "SHVA RW");
    pepa_set_tcp_recv_size(core, core->sockets.shva_rw, "SHVA");

    /* Add writing fd to epoll set */
    if (0 != epoll_ctl_add(epoll_fd, core->sockets.shva_rw, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP)) {
        slog_error_l("[SHVA] Can not add RW socket to epoll set");
        return (-1);
    }
    return (0);
}

static void pepa_emulator_shva_thread_cleanup(__attribute__((unused))void *arg)
{
    shva_rw_thread_clean_t *cargs = (shva_rw_thread_clean_t *)arg;
    pepa_core_t *core = pepa_get_core();

    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    SHVA CLEANUP                  $$$$$$$$$");

    int rc_remove = epoll_ctl(cargs->eventfd, EPOLL_CTL_DEL, cargs->sock_listen, NULL);

    if (rc_remove) {
        slog_warn_l("%s: Could not remove socket (FD = %d) from epoll set", "OUT RW", core->sockets.shva_rw);
    }

    int32_t rc = pepa_socket_shutdown_and_close(cargs->eventfd, "EMU SHVA");
    if (PEPA_ERR_OK != rc) {
        slog_error_l("[SHVA CLEANUP] Could not close listening socket");
    }

    pepa_reading_socket_close(core->sockets.shva_rw, "SHVA RW");
    core->sockets.shva_rw = FD_CLOSED;

    emu_set_shva_main_status(cargs->emu, ST_NO);
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

static void *pepa_emulator_shva(void *arg)
{
    volatile int iter = 0;
    size_t rx = 0;
    //size_t tx = 0;
    size_t reads = 0;
    // size_t writes = 0;
    emu_t *emup = arg;

    //pthread_t tid = pthread_self();

    TESTP_MES(emup, NULL, "SHVA: Argument is NULL");

    pepa_core_t *core = emup->core;

    shva_rw_thread_clean_t cargs;
    int32_t rc = -1;

    struct sockaddr_in s_addr;
    //int                    sock_listen = FD_CLOSED;

    emu_set_shva_main_status(emup, ST_STARTING);

    pthread_block_signals("SHVA");

    struct epoll_event events[EVENTS_NUM];
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    cargs.eventfd = epoll_fd;

    cargs.buf = buf_new(core->emu_max_buf + 1);
    if (NULL == cargs.buf) {
        slog_error_l("Can not allocate buf_t");
        abort();
    }

    // buf_t *buf = cargs.buf;

    pthread_setname_np(pthread_self(), "SHVA");
    pthread_cleanup_push(pepa_emulator_shva_thread_cleanup, &cargs);

    emu_set_shva_main_status(emup, ST_WAITING);

    do {
        do {
            slog_note_l("[SHVA] OPEN LISTENING SOCKET");
            core->sockets.shva_listen = pepa_open_listening_socket(&s_addr, core->shva_thread.ip_string, core->shva_thread.port_int, 1, __func__);
            if (core->sockets.shva_listen < 0) {
                slog_note_l("[SHVA] Could not open listening socket, waiting...");
                usleep(1000);
            }
        } while (core->sockets.shva_listen < 0); /* Opening listening soket */


        // core->sockets.shva_listen = sock_listen;

        slog_note_l("[SHVA] Opened listening socket");

        if (0 != epoll_ctl_add(epoll_fd, core->sockets.shva_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
            close(epoll_fd);
            slog_fatal_l("[SHVA] Could not add listening socket to epoll");
            pthread_exit(NULL);
        }

        do {
            /* The controlling thread can ask us to wait;
               It supervises all other threads and makes decisions */
            int status = emu_get_shva_main_status(emup);
            switch (status) {
                case ST_WAITING:
                    usleep(10000);
                    continue;
                case ST_STOPPED:
                    goto shva_main_exit;
                case ST_RESET:
                    goto reset;
                default:
                    break;
            }

            const int event_count = epoll_wait(epoll_fd, events, EVENTS_NUM, 1000);
            iter++;

            /* No events, exited by timeout */
            if (0 == event_count) {

                /* Emulate socket closing */
                if (SHVA_SHOULD_EMULATE_DISCONNECT()) {
                    // slog_debug_l("SHVA: EMULATING DISCONNECT");
                    pepa_emulator_disconnect_mes("SHVA");
                    goto reset;
                }
            }

            /* Interrupted by a signal */
            if (event_count < 0 && EINTR == errno) {
                continue;
            }

            /* An error happened, we close the listening socket and remove it from the epoll */
            if (event_count < 0) {
                slog_fatal_l("[SHVA] error on wait: %s", strerror(errno));
                rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, core->sockets.shva_listen, NULL);

                if (rc) {
                    slog_warn_l("[SHVA] Could not remove socket (FD = %d) from epoll set %d", core->sockets.out_write, epoll_fd);
                }
                goto reset;
            }

            /* If here, it means everything is OK and we have a connection on the listening socket, or a socket is broken */

            for (int i = 0; i < event_count; i++) {

                const int fd = events[i].data.fd;
                uint32_t evs = events[i].events;

                /* The any socket is disconnected - reset all sockets */
                if (evs & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    slog_error_l("The socket is disconnected");
                    goto reset;
                }

                /* IN event on listening, we should accept new connection */
                if (core->sockets.shva_listen == fd && (evs & EPOLLIN)) {
                    rc = pepa_emu_shva_accept(epoll_fd, core->sockets.shva_listen, core);
                    slog_note_l("Reading socket is connected");
                    continue;
                }  /* if (sock_listen == events[i].data.fd && events[i].events == EPOLLIN)*/

                /* If no RW socket, continue */
                if (FD_CLOSED == core->sockets.shva_rw) {
                    slog_note_l("[SHVA] RW socket is closed");
                    continue;
                }

                /* On event on RW socket, read */
                if ((core->sockets.shva_rw == fd) && (evs & EPOLLIN)) {
                    //slog_note_l("SHVA [thread ID = %lu]:::: receiving buf, iter: %d, event_count: %d, i: %d",
                    //            (unsigned long)tid, iter, event_count, i);

                    while (bytes_available_read(core->sockets.shva_rw) > 1000) {
                        //slog_note_l("[SHVA] READ: SOCKET READ AVAL  : %d", bytes_available_read(core->sockets.shva_rw));
                        rc = pepa_emulator_shva_read(core->sockets.shva_rw, (size_t *)&emup->bytes_rx_shva, &reads);
                        //slog_note_l("[SHVA] AFTER READ");
                        if (rc < 1) {
                            slog_error_l("[SHVA] Can not read from RW socket, reset sockets");
                            goto reset;
                        }
                        emup->bytes_rx_shva = rx;
                    }

                    continue;
                }

            } /* if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) */
            /* Write to RW socket */

            /* If no RW socket, continue */
            if (FD_CLOSED == core->sockets.shva_rw) {
                slog_note_l("[SHVA] The RW socket is closed");
                continue;
            }

            /* This flag can be tunred on / off for isolated debug */
            if (enable_shva_tx) {
                size_t writes;
                //slog_note_l("[SHVA] WRITE, SOCKET WRITE AVAL: %d", bytes_available_write (core->sockets.shva_rw));
                rc = pepa_emulator_shva_write(core->sockets.shva_rw, (size_t *)&emup->bytes_tx_shva, &writes);
                emu->num_shva_writes++;
                if (rc < 0) {
                    slog_error_l("[SHVA] Can not write to RW socket, reset sockets");
                    goto reset;
                }
            }

        } while (1);

        /* Emulate broken connection */
    reset:

        /* Close rw socket */
        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, core->sockets.shva_listen, NULL);

        if (rc) {
            slog_warn_l("[SHVA] Could not remove Listen socket (FD = %d) from epoll set %d", core->sockets.out_write, epoll_fd);
        }

        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, core->sockets.shva_rw, NULL);

        if (rc) {
            slog_warn_l("[SHVA] Could not remove RW socket (FD = %d) from epoll set %d", core->sockets.out_write, epoll_fd);
        }

        pepa_reading_socket_close(core->sockets.shva_rw, "SHVA RW");
        core->sockets.shva_rw = FD_CLOSED;

        rc = pepa_socket_shutdown_and_close(core->sockets.shva_listen, "SHVA MAIN");
        if (PEPA_ERR_OK != rc) {
            slog_error_l("[SHVA] Could not close listening socket");
        }
        core->sockets.shva_listen = FD_CLOSED;
        /* We set the status to STOPPED; the control thread may need to restart SHVA read/write threads */
        emu_set_shva_main_status(emup, ST_WAITING);

        emu_set_send_cnt(BUF_SRC_SHVA, 0, 0);
        emu_set_recv_cnt(BUF_SRC_SHVA, 0, 0);

    } while (1); /* Opening connection and acceptiny */

    /* Now we can start send and recv */

shva_main_exit:

    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}

/*** IN ***/

static int32_t in_start_connection(void)
{
    pepa_core_t *core = pepa_get_core();
    int sock;
    do {
        sock = pepa_open_connection_to_server(core->in_thread.ip_string->data, core->in_thread.port_int, __func__);
        if (sock < 0) {
            slog_note_l("[IN]: Could not connect to IN; waiting...");
            sleep(5);
        }
    } while (sock < 0);

    pepa_set_tcp_timeout(sock);
    pepa_set_tcp_send_size(core, sock, "IN");

    return (sock);
}

typedef struct {
    int fd;
    int my_num;
    emu_t *emu;
    buf_t *buf;

}
in_thread_args_t;

static void pepa_emulator_in_thread_cleanup(__attribute__((unused))void *arg)
{
    in_thread_args_t *args = arg;

    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
    slog_note("$$$$$$$    IN_FORWARD CLEANUP            $$$$$$$$$");

    slog_note("[IN] Going to close IN[%d] socket (FD = %d) port %d", args->my_num, args->fd, pepa_find_socket_port(args->fd));

    pepa_reading_socket_close(args->fd, "EMU IN TRHEAD");
#if 0 /* SEB */ /* 26/10/2024 */
    int rc = buf_free(args->buf);
    if (rc) {
        slog_error_l("Can't free buf_t");
    }
#endif /* SEB */ /* 26/10/2024 */

    emu_set_in_status(args->emu, args->my_num, ST_STOPPED);
    free(args);
    slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

/* If this flag is ON, all IN threads should emulate sicconnection and sleep for 5 seconds */
uint32_t in_thread_disconnect_all = 0;

/* Create 1 read/write listening socket to emulate SHVA server */
__attribute__((noreturn))
static void *pepa_emulator_in_thread(__attribute__((unused))void *arg)
{
    emu_cnt_t buf_count = 0;
    in_thread_args_t *args = arg;
    const pepa_core_t *core = pepa_get_core();
    // int         in_socket = FD_CLOSED;
    args->fd = FD_CLOSED;

    args->buf = buf_new(core->emu_max_buf + 1);
    if (NULL == args->buf) {
        slog_error_l("Can not allocate buf_t");
        abort();
    }

    // buf_t *buf = args->buf;

    emu_set_in_status(args->emu, args->my_num, ST_STARTING);

    pthread_cleanup_push(pepa_emulator_in_thread_cleanup, args);

    // const int32_t *my_num     = (int32_t *)arg;
    char my_name[32] = {
        0
    };

    uint64_t writes = 0;
    uint64_t rx = 0;

    emu_set_int_signal_handler();
    sprintf(my_name, "IN[%.2d]", args->my_num);

    pthread_setname_np(pthread_self(), my_name);

    pthread_block_signals(my_name);

    /* In this thread we read from socket as fast as we can */

    do { /* Opening connection */
        /* Note: the in_start_connection() function can not fail; it blocking until connection opened */
        args->fd = in_start_connection();
        slog_note_l("%s Opened connection to IN: (FD = %d), port: %d", my_name, args->fd, pepa_find_socket_port(args->fd));

        slog_info_l("[%s] is UP, start WAITING status", my_name);

        emu_set_in_status(args->emu, args->my_num, ST_WAITING);

        do { /* Run transfer */
            /* Disconnection of all IN thread is required */
            if (in_thread_disconnect_all > 0) {
                break;
            }

            /* The controlling thread can ask us to wait;
               It supervises all other threads and makes decisions */
            int status = emu_get_in_status(args->emu, args->my_num);
            switch (status) {
                case ST_WAITING:
                    usleep(10000);
                    continue;
                case ST_STOPPED:
                    slog_note_l("%s Thread asked to stop", my_name);
                    goto in_stop;
                default:
                    break;
            }

            while (0 == enable_in_tx) {
                sleep(1);
            }

            // size_t buf_size = ((size_t)rand() % core->emu_max_buf); //core->emu_max_buf;
            size_t buf_size = emu_random_buf_size();
            buf_t *buf = buf_new(buf_size);
            if (NULL == buf) {
                slog_error_l("Can not allocate buf_t");
                abort();
            }

            pthread_mutex_lock(&emu->in_threads_lock);

            buf_count = emu_get_send_cnt(BUF_SRC_IN, args->my_num);

            if (PEPA_ERR_OK != pepa_emulator_generate_buffer_buf(buf,                               /* Existing buffer */
                                                                 buf_size,                          /* The buffer size we want to generate */
                                                                 buf_count,    /* Counter */
                                                                 BUF_SRC_IN,                        /* Source == IN */
                                                                 args->my_num)/* IN Instance number */) {
                slog_error_l("Error when buffer generated");
                abort();
            }

            emu->num_in_writes++;

            const buf_head_t *head = BUF_HEAD(buf, 0);
            if (0 == head->checksum) {
                slog_error_l("On send: checksum == 0");
                abort();
            }

            ssize_t rc = send_exact(args->fd, buf->data, buf->used);

            emu->bytes_tx_in += rc;

            writes++;

            if (rc != buf->used) {
                slog_error_l("Could not sent the full buffer: its size is %ld, only %ld sent", buf->used, rc);
                rc = buf_free(buf);
                if (rc) {
                    slog_error_l("Can not free buf_t");
                }
                goto reset_socket;
                // abort();
            }

            if (rc < 0) {
                slog_error_l("%s Could not send buffer to SHVA, error: %s (%d)", my_name, strerror(errno), errno);
                //break;
                rc = buf_free(buf);
                if (rc) {
                    slog_error_l("Can not free buf_t");
                }
                goto reset_socket;
            }

            if (0 == rc) {
                slog_error_l("%s Send 0 bytes to SHVA, error: %s (%d)", my_name, strerror(errno), errno);
                // usleep(10000);
                //break;
                rc = buf_free(buf);
                if (rc) {
                    slog_error_l("Can not free buf_t");
                }
                goto reset_socket;
            }

            emu_save_buf_to_zhash(emu, buf);
            /* Update the buffer's counter */
            buf_count++;
            emu_set_send_cnt(BUF_SRC_IN, args->my_num, buf_count);

            rx += (uint64_t)rc;

            /* Emulate socket closing */
            if (SHOULD_EMULATE_DISCONNECT()) {
                pepa_emulator_disconnect_mes(my_name);
                goto reset_socket;
                // break;
            }

            /* Eulate ALL IN sockets disconnect */
            if (SHOULD_EMULATE_DISCONNECT()) {
                pepa_emulator_disconnect_mes("ALL IN");
                in_thread_disconnect_all = core->emu_in_threads;
                goto reset_socket;
                // break;
            }

            if (core->emu_timeout > 0) {
                // usleep(core->emu_timeout);
            }

            // sleep(2);

            usleep(1000);
            pthread_mutex_unlock(&emu->in_threads_lock);
        } while (1); /* Generating and sending data */



    reset_socket:
        pthread_mutex_unlock(&emu->in_threads_lock);
        buf_count = emu_get_recv_cnt(BUF_SRC_IN, args->my_num);

        slog_warn_l("%s Closing connection, buffers sent: %d", my_name, buf_count);
        close(args->fd);
        args->fd = FD_CLOSED;

        emu_set_recv_cnt(BUF_SRC_IN, args->my_num, 0);
        emu_set_send_cnt(BUF_SRC_IN, args->my_num, 0);

        sleep(1);

        /* If 'disconnect all IN threads' counter is UP, decrease it */
        if (in_thread_disconnect_all > 0) {
            in_thread_disconnect_all--;
        }
        // sleep(5);
    } while (1);
in_stop:
    pthread_cleanup_pop(0);
    pthread_exit(NULL);
}


/***********************************************************************************************************/
/**** REIMPLEMENTATION ****/
/***********************************************************************************************************/


/*** The OUT Thread ***/

static int start_out_thread(emu_t *emu)
{
    int rc;
    slog_info_l("Starting OUT thread");
    // pepa_out_external_thread
    // rc = pthread_create(&emu->out_id, NULL, pepa_emulator_out_thread, emu);
    rc = pthread_create(&emu->out_id, NULL, pepa_out_external_thread, emu);
    if (0 == rc) {
        slog_note_l("OUT thread created");
    } else {
        pepa_parse_pthread_create_error(rc);
        return (-1);
    }
    slog_note_l("OUT thread is started");
    return (0);
}

static int kill_a_thread(pthread_t id, const char *name)
{
    int idx;
    int rc;
    slog_info_l("Stopping %s thread", name);
    rc = pthread_cancel(id);
    if (0 == rc) {
        slog_note_l("%s thread cancel request is sent", name);
    } else {
        pepa_parse_pthread_create_error(rc);
        return (-1);
    }

    /* Wait until the thread is terminated */

    for (idx = 0; idx < 100; idx++) {
        usleep(1000);
        /* pthread_kill(X, 0) tests the pthread status. If the thread is running, it return 0  */
        if (0 != pthread_kill(id, 0)) {
            slog_note_l("%s thread is killed", name);
            return (0);
        }
    }

    slog_note_l("%s thread is NOT killed", name);
    return (1);
}

static int kill_out_thread(emu_t *emu)
{
    return (kill_a_thread(emu->out_id, "OUT"));
}

static int start_shva(emu_t *emu)
{
    int rc;
    slog_info_l("Starting SHVA socket thread");
    rc = pthread_create(&emu->shva_id, NULL, pepa_emulator_shva, emu);
    if (0 == rc) {
        slog_note_l("SHVA socket thread created");
    } else {
        slog_note_l("SHVA socket thread was NOT created");
        pepa_parse_pthread_create_error(rc);
        return (-1);
    }
    return (0);
}

static int kill_shva(emu_t *emu)
{
    return (kill_a_thread(emu->shva_id, "SHVA SOCKET"));
}

static int start_in_threads(emu_t *emu)
{
    size_t i;
    int rc;

    for (i = 0; i < emu->in_number; i++) {
        if (ST_NO != emu->in_stat[i]) {
            continue;
        }

        slog_info_l("Starting IN[%lu] thread", i);
        in_thread_args_t *arg = calloc(sizeof(in_thread_args_t), 1);
        if (NULL == arg) {
            slog_error_l("Can not allocate IN arguments structure");
            abort();
        }

        arg->my_num = i;
        arg->emu = emu;

        rc = pthread_create(&emu->in_ids[i], NULL, pepa_emulator_in_thread, arg);
        if (0 == rc) {
            slog_note_l("IN[%lu] thread is created", i);
        } else {
            pepa_parse_pthread_create_error(rc);
            return (-1);
        }
    }
    slog_note_l("IN threads are started");
    return (0);
}

static int kill_in_threads(emu_t *emu)
{
    size_t i;

    for (i = 0; i < emu->in_number; i++) {
        slog_info_l("Stopping IN[%lu] thread", i);
        char name[64];
        sprintf(name, "%s[%zu]", "IN", i);
        int rc = kill_a_thread(emu->in_ids[i], name);

        if (0 == rc) {
            slog_note_l("IN[%lu] thread is stopped", i);
        } else {
            slog_error_l("IN[%lu] thread is NOT stopped", i);
        }
    }

    return (0);
}

#define MONITOR_SLEEP (1)
static void *emu_monitor(void *arg)
{
    emu_t *emup = arg;

    do {
        sleep(MONITOR_SLEEP);

        slog_note("MONITOR: RW: [IN w: %d] [OUT-EXT w: %d r:%d] SHVA [w: %d r: %d]",
                  emup->num_in_writes,
                  emup->num_out_writes,
                  emup->num_out_writes,
                  emup->num_shva_writes,
                  emup->num_shva_reads);

#if 0 /* SEB */ /* 15/11/2024 */
        slog_note("MONITOR: BYTES [IN tx: %d] [OUT-EXT rx: %d tx: %d] OUT-IN [rx: %d tx: %d] SHVA [rx: %d tx: %d]",
                  emup->bytes_tx_in,
                  emup->bytes_rx_out_external,
                  emup->bytes_tx_out_external,
                  emup->bytes_rx_out_internal,
                  emup->bytes_rx_out_internal,
                  emup->bytes_rx_shva,
                  emup->bytes_tx_shva);
#endif /* SEB */ /* 15/11/2024 */


    } while (1);
    return (NULL);
}

static int start_monitor_thread(emu_t *emu)
{
    return (pthread_create(&emu->monitor_id, NULL, emu_monitor, emu));
}

static int kill_monitor(emu_t *emu)
{
    return (kill_a_thread(emu->monitor_id, "EMU MONITOR"));
}

static void in_st_print(const emu_t *emup, char *buf)
{
    size_t offset = 0;
    for (size_t idx = 0; idx < emup->in_number; idx++) {
        offset += sprintf(buf + offset, "[EMU CONTROL]: IN[%zu]       = %s\n", idx, st_to_str(emup->in_stat[idx]));
    }
}

#define PRINT_STATUSES (0)

static void emu_control_pr_statuses(emu_t *emu)
{
    if (!PRINT_STATUSES) {
        return;
    }

    char *buf = calloc(1024, 1);

    slog_note_l("[EMU CONTROL]: run");

    int st_out = emu_get_out_status(emu);
    int st_shva_socket = emu_get_shva_main_status(emu);
    int st_shva_read = emu_get_shva_read_status(emu);
    int st_shva_write = emu_get_shva_write_status(emu);

    in_st_print(emu, buf);

    slog_note_l("\n[EMU CONTROL]: out         = %s\n"
                "[EMU CONTROL]: shva socket = %s\n"
                "[EMU CONTROL]: shva read   = %s\n"
                "[EMU CONTROL]: shva write  = %s\n"
                "%s"
                "[EMU CONTROL]: all IN NO?  = %d\n",
                st_to_str(st_out),
                st_to_str(st_shva_socket),
                st_to_str(st_shva_read),
                st_to_str(st_shva_write),
                buf,
                emu_if_in_all_have_status(emu, ST_NO));
    free(buf);
}

#define L_TRACE_ENABLE (0)
#define L_TRACE() do{ if(L_TRACE_ENABLE)slog_note_l("[EMU CONTROL] Line Debug: Line %d",  __LINE__); }while(0)

#define POST_SLEEP_TIME (50000)
#define POST_SLEEP() do{usleep(POST_SLEEP_TIME);}while(0)

static int controll_state_machine(emu_t *emu)
{
    start_monitor_thread(emu);
    do {
        // char *buf = calloc(1024, 1);
        //int counter = 0;
        // sleep(1);
        usleep(50000);
        if (emu->should_exit) {
            kill_monitor(emu);
            kill_in_threads(emu);
            kill_shva(emu);
            kill_out_thread(emu);
            sleep(1);
            return (0);
        }

        // slog_note_l("[EMU CONTROL]: run");

        int st_out = emu_get_out_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 1");

        int st_shva_socket = emu_get_shva_main_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 2");

        // int st_shva_read   = emu_get_shva_read_status(emu);
        //slog_note_l("[EMU CONTROL]: got statuses: 3");

        // int st_shva_write  = emu_get_shva_write_status(emu);


        emu_control_pr_statuses(emu);

        /* Start OUT thread */
        if (ST_NO == st_out) {
            slog_note_l("[EMU CONTROL] Starting OUT thread: ST_NO -> ST_WAITING");
            start_out_thread(emu);
            POST_SLEEP();
            //continue;
        }

        //slog_note_l("[EMU CONTROL] Line Debug: Line %d",  __LINE__);

        // slog_note_l("[EMU CONTROL]: 1");

        /* Start IN threads */
        if (emu_if_in_all_have_status(emu, ST_NO)) {
            slog_note_l("[EMU CONTROL] Starting running of INs: ST_NO -> ST_WAITING");
            start_in_threads(emu);
            POST_SLEEP();
            // continue;
        }

        /* Start SHVA socket thread; the main thread will start READ and WRITE threads */
        if (ST_NO == st_shva_socket) {
            slog_note_l("[EMU CONTROL] Starting SHVA main (socket) thread: ST_NO -> ST_WAITING");
            start_shva(emu);
            POST_SLEEP();
            //continue;
        }

        /* We DO NOT start here SHVA READ / WRITE; we start them only after INs are running */

        /*** EXCEPTIONS: stop / restart threads ***/

        /* If SHVA socket(s) are degraded, the Socket thread closes them and set statys RESET.
           We should restart both SHVA READ and WRITE threads, and also IN threads, and reset the Socket thread status */
        if (ST_RESET == st_shva_socket) {

            /* Reload IN threads */
            kill_in_threads(emu);
            POST_SLEEP();

            start_in_threads(emu);
            POST_SLEEP();

            emu_set_all_in(emu, ST_RUNNING);
            continue;
        }

        /* SHVA Socket: After reseting, set into WAITING state. It star depends on OUT thread state, we test it later */

        if (ST_READY == st_shva_socket) {
            emu_set_shva_main_status(emu, ST_WAITING);
            POST_SLEEP();
        }

        /* If SHVA socket reseted the socket and ready to continue, and writer and reader are existing, start them */

        /* OUT is WAITING, and SHVA is RUNNING: the OUT was restarted, set all SHVA threads to WAITING */

        if (ST_WAITING == st_out && ST_RUNNING == st_shva_socket) {
            //slog_note_l("[EMU CONTROL] Starting running of OUT: ST_WAITING -> ST_RUNNING");
            emu_set_shva_main_status(emu, ST_WAITING);
            POST_SLEEP();
        }

        /* 1. OUT thread is waiting and SHVA read is waiting: start OUT */

        if (ST_WAITING == st_out && ST_WAITING == st_shva_socket) {
            //slog_note_l("[EMU CONTROL] Starting running of OUT: ST_WAITING -> ST_RUNNING");
            emu_set_out_status(emu, ST_RUNNING);
            POST_SLEEP();
        }

        /* 2. OUT thread is running and SHVA socket is waiting: start SHVA socket, we are ready */

        if (ST_RUNNING == st_out && ST_WAITING == st_shva_socket) {
            //slog_note_l("[EMU CONTROL] Starting running of SHVA main: ST_WAITING -> ST_RUNNING");
            emu_set_shva_main_status(emu, ST_RUNNING);
            POST_SLEEP();
            // continue;
        }

        /* 4. SHVA write is running and any IN are waiting: start all IN threads */

        if (ST_RUNNING == st_shva_socket && emu_if_in_any_have_status(emu, ST_WAITING)) {
            // slog_note_l("[EMU CONTROL] Starting running of INs: ST_WAITING -> ST_RUNNING");
            emu_set_all_in_state_to_state(emu, ST_WAITING, ST_RUNNING);
            POST_SLEEP();
        }

        /* 5. SHVA write is stopped and any of IN is waiting: stop all IN threads */

        if (ST_STOPPED == st_shva_socket && emu_if_in_any_have_status(emu, ST_RUNNING)) {
            // slog_note_l("[EMU CONTROL] Stopping running of INs: ST_RUNNING -> ST_WAITING");
            emu_set_all_in_state_to_state(emu, ST_RUNNING, ST_WAITING);
            POST_SLEEP();
        }

        // slog_note_l("[EMU CONTROL]: end");

    } while (1);
    L_TRACE();
}

#if 0 /* SEB */ /* 27/10/2024 */
static int emu_rand(int min, int max){
    int val;
    do {
        val = rand() % max;

    } while (val < min || val > max);
    return val;
}
#endif /* SEB */ /* 27/10/2024 */

//#define MAX_SLEEP (120)
//#define MIN_SLEEP (10)
/* Randonly close sockets */
#if 0 /* SEB */ /* 25/10/2024 */
static void emu_saboteur(void *arg){
    pepa_core_t *core = arg;
    int sleep_time = emu_rand(MIN_SLEEP, MAX_SLEEP);

    sleep(sleep_time);

    int harm = emu_rand(0, 4);

    switch (harm) {
        case 0: /* Close OUT Listen socket */
        pepa_socket_shutdown_and_close(core->sockets.out_listen, "OUT LISTEN");
        core->sockets.out_listen = FD_CLOSED;
        break;
        case 1: /* Close OUT RW socket */
        pepa_socket_shutdown_and_close(core->sockets.out_write, "OUT WRITE");
        core->sockets.out_write = FD_CLOSED;
        break;
        case 2: /* Close SHVA RW socket */
        pepa_socket_shutdown_and_close(core->sockets.shva_rw, "OUT WRITE");
        core->sockets.shva_rw = FD_CLOSED;
        break;
        case 3: /* Close SHVA Listen socket */
        pepa_socket_shutdown_and_close(core->sockets.shva_listen, "OUT WRITE");
        core->sockets.shva_listen = FD_CLOSED;
        break;
        case 4:
    { /* Close one of IN sockets */
        int num = emu_rand(0, core->emu_in_threads);
        // pepa_socket_shutdown_and_close(emu->, "OUT WRITE");
    }
        break;
        case 5: /* Close all IN sockets */
        break;
        default:
        break;
    }


}
#endif /* SEB */ /* 25/10/2024 */

int main(int argi, char *argv[])
{
    pthread_t out_internal_thread_t;
    int rc;
    slog_init("EMU", SLOG_FLAGS_ALL, 0);
    pepa_core_init();
    pepa_core_t *core = pepa_get_core();

    rc = pepa_parse_arguments(argi, argv);
    if (rc < 0) {
        slog_fatal_l("Could not parse");
        return (rc);
    }

    if (NULL != core->config) {
        rc = pepa_read_config(core);
    }
    if (rc) {
        slog_error_l("Could not read / parse config file %s", core->config);
        return (-1);
    }

    pepa_config_slogger(core);
    lorem_ipsum_len = strlen(lorem_ipsum);
    //lorem_ipsum_buf = buf_new((buf_s64_t)core->emu_max_buf);
    //pepa_emulator_generate_buffer_buf(lorem_ipsum_buf, core->emu_max_buf);

    emu_set_int_signal_handler();

    srand(17);
    /* Somethime random can return predictable value in the beginning; we skip it */
    rc = rand() + rand() + rand() + rand() + rand();

    pepa_set_rlimit();

    emu = emu_t_allocate(core->emu_in_threads, core);
    if (NULL == emu) {
        slog_error_l("Can not allocate emu_t");
        abort();
    }

    /* Create OUT pipes */
#if 1 /* SEB */ /* 15/11/2024 */
    rc = pipe(pipe_out);
    if (rc) {
        slog_fatal_l("Can not create OUT pipe: %s", strerror(errno));
        exit(1);
    }
#endif /* SEB */ /* 15/11/2024 */
    pthread_create(&out_internal_thread_t, NULL, pepa_emu_out_internal_thread, emu);
    usleep(10000);

    controll_state_machine(emu);
    emu_t_free(emu);
    return (0);

    slog_warn_l("We should never be here, but this is the fact: we are here, my dear friend. This is the time to say farewell.");
    return (10);
}
