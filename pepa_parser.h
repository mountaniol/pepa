#ifndef _PEPA_PARSE_H__
#define _PEPA_PARSE_H__

#include <stdint.h>
void pepa_show_help(void);

__attribute__((warn_unused_result))
long pepa_string_to_int_strict(char *s, int32_t *err);

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Get argument IP:PORT, extract PORT as an integer,
 *  	  and return the port
 * @param char* _argument Argument in form "ADDRESS:PORT",
 *  		  like "1.2.3.4:5566"
 * @return int32_t  The PORT part of the
 *  	   argument as an integer; A negative value on error
 */

__attribute__((warn_unused_result))
int32_t pepa_parse_ip_string_get_port(const char *argument);

__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Get argument IP:PORT, extract IP as a string,
 *  	  and return buf_t containing the IP string
 * @param char* _argument Argument in form "ADDRESS:PORT",
 *  		  like "1.2.3.4:5566"
 * @return buf_t* String buffer containing ADDRESS part of the
 *  	   argument as a string; NULL on an error
 */
buf_t *pepa_parse_ip_string_get_ip(const char *_argument);

__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Parse command line arguments, init pepa_core_t values
 * @param int32_t argi  Number of command line arguments
 * @param char*[] argv  Pointer to array of char* arguments
 * @return int32_t PEPA_ERR_OK if parsed and all required arguments
 *  	   are provided, a negative error code otherwise
 * @details 
 */
int32_t pepa_parse_arguments(int32_t argi, char *argv[]);

__attribute__((warn_unused_result))
int32_t pepa_config_slogger(pepa_core_t *core);
int32_t pepa_config_slogger_daemon(pepa_core_t *core);

void pepa_print_version(void);

#endif /* _PEPA_PARSE_H__ */
