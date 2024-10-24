#ifndef PEPA_EMULATOR_H__
#define PEPA_EMULATOR_H__

#include <pthread.h>
#include "pepa_core.h"

#define PR_DOWN (0)
#define PR_UP (1)

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

/**
 * @author Sebastian Mountaniol (21/10/2024)
 * @struct
 * @brief This structure used to control all emulator threads
 * @details 
 */
typedef struct {
    pthread_t control_thread_id;
    pthread_mutex_t lock; /**< This structure is shared between tthreads; lock needed to change it */

    int changes; /**< When a thread changes this structure, it should increase this counter. This way controllong thread "knows" that it changed */
    char shva_read_status; /**< Status of SHVA thread */
    char shva_write_status; /**< Status of SHVA thread */
    char shva_main_status; /**< Status of SHVA thread */
    char out_status; /**< Status od OUT thread */
    char *in_stat ; /**< Array of statuses of IN thread */

    pthread_t shva_id;
    pthread_t shva_read;
    pthread_t shva_write;
    pthread_t out_id;
    pthread_t *in_ids;

    size_t in_number; /* Number of IN threads */
    pepa_core_t *core;
    int should_exit; /**< Terminating EMU, if it is not 0  */
} emu_t;

#endif /* PEPA_EMULATOR_H__ */
