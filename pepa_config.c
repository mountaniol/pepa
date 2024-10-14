#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#include "slog/src/slog.h"
#include "iniparser.h"
#include "pepa_config.h"
#include "pepa_core.h"

/**
 * @author se (9/17/24) 
 * @brief Read and parse config file 
 * @param core   Core file structure
 * @return int 0 if config read and parsed; a negative in case 
 *         of error
 */
int pepa_read_config(pepa_core_t *core)
{
    char *value;
    int  rc;

    if (NULL == core->config) {
        slog_error_l("No config file is specified");
    }

    struct read_ini *ini_r = NULL;
    struct ini      *ini   = read_ini(&ini_r, core->config);

    if (NULL == ini) {
        slog_error_l("Can not read INI file %s",  core->config);
        return -1;
    }

    value = ini_get_value(ini, "config", "readers");
    TESTP_MES(value,  -1,  "Can not read from condig 'readers'");

    slog_note_l("Parsing %s", value);
    rc = sscanf(value, "%ld", &core->readers_preopen);
    if (0 == rc) {
        slog_note_l("Could not parse %s", value);
        return -1;
    }

    value = ini_get_value(ini, "config", "id");

    if (value) {
        slog_note_l("Parsing %s", value);

        rc = sscanf(value, "%x", &core->use_id);

        if (0 == rc) {
            slog_error_l("Could not convert %s to int", value);
        }
    }

    value = ini_get_value(ini, "config", "id_val");
    if (value) {
        slog_note_l("Parsing %s", value);
        rc = sscanf(value, "%X", &core->id_val);
        if (0 == rc) {
            slog_error_l("Could not convert %s to int", value);
        }
    }

    value = ini_get_value(ini, "config", "ticket");
    if (value) {
        slog_note_l("Parsing %s", value);
        rc = sscanf(value, "%d", &core->use_ticket);
        if (0 == rc) {
            slog_error_l("Could not convert %s to int", value);
        }

    } 

    printf("CONFIG: readers: %ld\n", core->readers_preopen);
    printf("CONFIG: add PEPA id to every buffer = %s\n", core->use_id ? "YES" : "NO");
    printf("CONFIG: PEPA id value = %X\n", (unsigned int)core->id_val);
    printf("CONFIG: add a ticket to every buffer = %s\n", core->use_ticket ? "YES" : "NO");

    return 0;
}

