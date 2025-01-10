#ifndef _PEPA3_H__
#define _PEPA3_H__

#include "pepa_core.h"

enum {
    REINIT_SHVA_RW = 3000, /* We need to reinit the SHVA socket and all dependent components */
    REINIT_OUT_RW, /* We need to reinit the OUT Read-Write socket and all dependent components */
    REINIT_OUT_LISTEN, /* We need to reinit the OUT Listening socket and all dependent components */
    REINIT_ALL = REINIT_OUT_LISTEN, /* We need to reinit all the sockets and all dependent components */
    REINIT_IN_RW, /* We need to reinit an IN Read-Write socket and all dependent components */
    REINIT_IN_LISTEN, /* We need to reinit the IN Listening socket and all dependent components */
};

int pepa4_restart_sockets(pepa_core_t *core, int start_from);
int pepa4_close_sockets(pepa_core_t *core);
void pepa4_close_needed_sockets(pepa_core_t *core, int what);

//__attribute__((noreturn))
void  pepa4_transfer_loop2(pepa_core_t *core);

#endif /* _PEPA3_H__ */
