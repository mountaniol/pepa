#ifndef _PEPA_PARSE_H__
#define _PEPA_PARSE_H__

long int pepa_string_to_int_strict(char *s, int *err);

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Get argument IP:PORT, extract PORT as an integer,
 *  	  and return the port
 * @param char* _argument Argument in form "ADDRESS:PORT",
 *  		  like "1.2.3.4:5566"
 * @return int  The PORT part of the
 *  	   argument as an integer; A negative value on error
 */

int pepa_parse_ip_string_get_port(const char *argument);

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

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Parse command line arguments, init pepa_core_t values
 * @param int argi  Number of command line arguments
 * @param char*[] argv  Pointer to array of char* arguments
 * @return int PEPA_ERR_OK if parsed and all required arguments
 *  	   are provided, a negative error code otherwise
 * @details 
 */
int pepa_parse_arguments(int argi, char *argv[]);

void pepa_print_version(void);

#endif /* _PEPA_PARSE_H__ */
