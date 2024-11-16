#ifndef _PEPA3_H__
#define _PEPA3_H__

#include "pepa_core.h"

enum {
    REINIT_SHVA_RW = 3000,
    REINIT_OUT_RW,
    REINIT_OUT_LISTEN,
    REINIT_ALL = REINIT_OUT_LISTEN,
    REINIT_IN_RW,
    REINIT_IN_LISTEN,
};

int pepa4_restart_sockets(pepa_core_t *core, int start_from);
int pepa4_close_sockets(pepa_core_t *core);
void pepa4_close_needed_sockets(pepa_core_t *core, int what);

__attribute__((noreturn))
void  pepa4_transfer_loop2(pepa_core_t *core);

#endif /* _PEPA3_H__ */
