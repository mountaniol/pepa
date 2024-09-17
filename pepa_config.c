#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#include "slog/src/slog.h"
#include "confuse.h"
#include "pepa_config.h"
#include "pepa_core.h"


/**
 * @author se (9/17/24) 
 * @brief Read and parse config file 
 * @param core   Core file structure
 * @return int 0 if config read and parsed; a negative in case 
 *         of error
 */
int pepa_read_config(pepa_core_t *core) {
    cfg_bool_t use_id = cfg_false;
    long int id_val = 0;
    cfg_bool_t use_ticket = cfg_false;
    cfg_t         *cfg    = NULL;

    TESTP(core->config,  -1);

    cfg_opt_t     opts[] = {
        CFG_SIMPLE_INT("readers", &core->readers_preopen),
        CFG_SIMPLE_BOOL("id", &use_id),
        CFG_SIMPLE_INT("id_val", &id_val),
        CFG_SIMPLE_BOOL("ticket", &use_ticket),
        CFG_END()
    };


#ifdef LC_MESSAGES
    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");
#endif

    cfg = cfg_init(opts, 0);
    TESTP(cfg,  -1);
    int rc = cfg_parse(cfg, core->config);
    if (0 != rc) {
        slog_error("Can not parse the config file %s",  core->config);
    }

    printf("CONFIG: readers: %ld\n", core->readers_preopen);
    printf("CONFIG: add PEPA id to every buffer = %s\n", use_id ? "YES" : "NO");
    printf("CONFIG: PEPA id value = %X\n", (unsigned int) id_val);
    printf("CONFIG: add a ticket to every buffer = %s\n", use_ticket ? "YES" : "NO");

    core->use_id = (use_id ? 1 : 0);
    core->id_val = (int) id_val;
    core->use_ticket = (use_ticket ? 1 : 0);

    return 0;
}

