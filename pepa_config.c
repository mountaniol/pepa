#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#include "slog/src/slog.h"
#include "confuse.h"
#include "pepa_config.h"
#include "pepa_core.h"

int pepa_read_config(const char *filename, pepa_core_t *core)
{
    cfg_t         *cfg    = NULL;

    TESTP(filename,  -1);

    cfg_opt_t     opts[]  = {
        CFG_SIMPLE_INT("readers", &core->readers_preopen),
        CFG_SIMPLE_INT("writers", &core->writers_preopen),
        CFG_END()
    };


#ifdef LC_MESSAGES
    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");
#endif

    cfg = cfg_init(opts, 0);
    TESTP(cfg,  -1);
    int rc = cfg_parse(cfg, filename);
    if (0 != rc) {
        slog_error("Can not parse the config file %s",  filename);
    }

    printf("readers: %ld\n", core->readers_preopen);
    printf("writers: %ld\n", core->writers_preopen);

    return 0;
}

