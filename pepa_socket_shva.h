#ifndef _PEPA_SOCKET_SHVA_H__
#define _PEPA_SOCKET_SHVA_H__

#include <semaphore.h>
#include <pthread.h>

#include "buf_t/buf_t.h"
#include "pepa_core.h"

void *pepa_shva_thread_new(__attribute__((unused))void *arg);

#endif /* _PEPA_SOCKET_SHVA_H__ */
