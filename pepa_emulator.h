#ifndef PEPA_EMULATOR_H__
#define PEPA_EMULATOR_H__

#include <pthread.h>
#include <stdint.h>
#include "zhash2.h"
#include "pepa_core.h"
#include "pepa_ticket_id.h"

/* This enum defines states of a thread;
   it is resposibility of thread itself to change the status */
enum {
	ST_NO = 1, /* The thread is not existing now */
	ST_STOPPED, /* The 'stopped state is initial state. If a thread exits, it must change its state to 'stopped' */
	ST_STARTING, /* The strad is preparing to run */
	ST_WAITING, /* The thread is waiting for something external, for example, another thread is started */
	ST_RUNNING, /* The thread is in running state */
	ST_READY, /* The thread is in operational mode: sockets(s) are created and it is sending / receiving buffers  */
	ST_RESET, /* The thread is in reset mode: close and open socket(s) and start a freash running  */
	ST_EXIT, /* The thread's socket is degraded; when the thread find this status, it must terminate */
};


//typedef int64_t emu_cnt_t;
typedef int32_t emu_cnt_t;
// typedef uint64_t emu_checksum_t;
typedef uint32_t emu_checksum_t;


/**
 * @author Sebastian Mountaniol (21/10/2024)
 * @struct
 * @brief This structure used to control all emulator threads
 * @details
 */
typedef struct {
	pthread_t control_thread_id;

	pthread_mutex_t in_threads_lock; /**< Only one IN thread can transfer buffer */
	pthread_mutex_t lock; /**< This structure is shared between tthreads; lock needed to change it */

	int changes; /**< When a thread changes this structure, it should increase this counter. This way controllong thread "knows" that it changed */
	char shva_read_status; /**< Status of SHVA thread */
	char shva_write_status; /**< Status of SHVA thread */
	char shva_main_status; /**< Status of SHVA thread */
	char out_status; /**< Status od OUT thread */
	char *in_stat; /**< Array of statuses of IN thread */

	/* Counters */
	/* IN counters aray */
	emu_cnt_t *cnt_in_sent; /**< How many buffers IN sent */
	emu_cnt_t *cnt_in_recv; /**< How many buffers IN rev */

	emu_cnt_t cnt_shva_sent; /**< How many buffers SHVA sent */
	emu_cnt_t cnt_shva_recv; /**< How many buffers SHVA recv */

	emu_cnt_t bytes_tx_shva; /**< How many bytes SHVA sent */
	emu_cnt_t bytes_rx_shva; /**< How many bytes SHVA recv */

	emu_cnt_t bytes_tx_out_external; /**< How many bytes OUT external sent */
	emu_cnt_t bytes_rx_out_external; /**< How many bytes OUT external recv */

	emu_cnt_t bytes_tx_out_internal; /**< How many bytes OUT internal sent */
	emu_cnt_t bytes_rx_out_internal; /**< How many bytes OUT internal recv */

	emu_cnt_t bytes_tx_in; /**< How many bytes IN sent */
	emu_cnt_t bytes_rx_in; /**< How many bytes IN recv */

	emu_cnt_t num_shva_reads;
	emu_cnt_t num_shva_writes;

	emu_cnt_t num_out_reads;
	emu_cnt_t num_out_writes;

	emu_cnt_t num_in_reads;
	emu_cnt_t num_in_writes;

	pthread_t monitor_id;
	pthread_t shva_id;
	pthread_t shva_read;
	pthread_t shva_write;
	pthread_t out_id;
	pthread_t *in_ids;

	size_t in_number; /* Number of IN threads */
	pepa_core_t *core;
	int should_exit; /**< Terminating EMU, if it is not 0  */
	ztable_t *zhash_bufs;
	size_t zhash_count;
} emu_t;


enum {
	BUF_SRC_START = 1077,
	BUF_SRC_IN, 	/* 1078 */
	BUF_SRC_SHVA, 	/* 1079 */
	BUF_SRC_FINISH
};

#define SHVA_BUF_PATTERN (0x22222222)
#define IN_BUF_PATTERN (0x55550000)

#define BUF_HEADER_START (0X99999999)
#define BUF_HEADER_MARK (0X77777777)

typedef struct  __attribute__((packed)) {
	uint32_t start; /* Mark of the header == BUF_HEADER_MARK */
	emu_checksum_t checksum; /* Checksum of the buffer's content; this header does not included in calculation */
	emu_cnt_t cnt; /**< Counter */
	emu_cnt_t cnt_global; /**< Counter */
	pepa_ticket_t ticket; /**< Header's ticket (not related to PEPA ticket!) */
	uint32_t src; /**< Source of this buffer: IN, SHVA */
	uint32_t instance; /**< In case of IN there could be several instances; this is the number of instance */
	uint32_t len; /* The full buffer len (i.e. buf_t->used) */
	uint32_t mark; /* Mark of the header == BUF_HEADER_MARK */
} buf_head_t;

#define BUF_HEAD(_Buf, _Offset) ((buf_head_t *) ((char *)_Buf->data + _Offset))

#endif /* PEPA_EMULATOR_H__ */
