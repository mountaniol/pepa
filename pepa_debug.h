#ifndef _PEPA_DEBUG_H__
#define _PEPA_DEBUG_H__

#include "slog/src/slog.h"
#include "pepa_core.h"
#include "buf_t/buf_t.h"

#define PEPA_TRY_ABORT() do{if(YES == pepa_if_abort()) {slogf("Abort in %s +%d\n", __FILE__, __LINE__);abort();} } while(0)

#endif /* _PEPA_DEBUG_H__ */
