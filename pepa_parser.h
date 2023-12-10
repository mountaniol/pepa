#ifndef _PEPA_PARSE_H__
#define _PEPA_PARSE_H__

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

#endif /* _PEPA_PARSE_H__ */
