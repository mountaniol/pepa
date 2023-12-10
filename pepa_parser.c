#include <errno.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/select.h>

#include "buf_t/se_debug.h"
#include "buf_t/buf_t.h"
#include "pepa_config.h"
#include "pepa_ip_struct.h"
#include "pepa_core.h"
#include "pepa_errors.h"
#include "pepa_debug.h"

#include "pepa_parser.h"

static void pepa_show_help(void)
{
	printf("Use:\n"
		   "--help  | -h - show this help\n"
		   "--shva  | -s - address of SHVA server to connect to in form: '1.2.3.4:7887'\n"
		   "--out   | -o - address of OUT listening socket, waiting for OUT stram connnection, in form '1.2.3.4:9779'\n"
		   "--in    | -i - address of IN  listening socket, waiting for OUT stram connnection, in form '1.2.3.4:3748'\n"
		   "--inim  | -n - max number of IN clients, by default 1024\n"
		   "--abort | -a - abort on errors\n");
}

static long int pepa_string_to_int_strict(char *s, int *err)
{
	char     *endptr;
	long int res;
	errno = 0;
	/* strtol detects '0x' and '0' prefixes */
	res = strtol(s, &endptr, 0);
	if ((errno == ERANGE && (res == LONG_MAX || res == LONG_MIN)) || (errno != 0 && res == 0)) {
		DE("Invalid input %s\n", s);
		perror("strtol");
		*err = errno;
		PEPA_TRY_ABORT();
		return -1;
	}

	if (endptr == s) {
		DE("Invalid input %s\n", s);
		*err = 1;
		PEPA_TRY_ABORT();
		return -2;
	}

	if ((size_t)(endptr - s) != strlen(s)) {
		DE("Only part of the string '%s' converted: strlen = %zd, converted %zd\n", s, strlen(s), (size_t)(endptr - s));
		*err = 1;
		PEPA_TRY_ABORT();
		return -3;
	}

	*err = PEPA_ERR_OK;
	return res;
}

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Get argument IP:PORT, extract PORT as an integer,
 *  	  and return the port
 * @param char* _argument Argument in form "ADDRESS:PORT",
 *  		  like "1.2.3.4:5566"
 * @return int  The PORT part of the
 *  	   argument as an integer; A negative value on error
 */

static int pepa_parse_ip_string_get_port(const char *argument)
{
	int  _err       = 0;
	char *colon_ptr = NULL;

	TESTP_ASSERT(argument, "NULL argument");

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		DE("Can not find : between IP and PORT: IP:PORT\n");
		PEPA_TRY_ABORT();
		return -1;
	}

	int port = pepa_string_to_int_strict(colon_ptr + 1, &_err);

	if (_err) {
		DE("Can't convert port value from string to int: %s\n", colon_ptr);
		PEPA_TRY_ABORT();
		return -1;
	}

	return port;
}

/**
 * @author Sebastian Mountaniol (12/10/23)
 * @brief Get argument IP:PORT, extract IP as a string,
 *  	  and return buf_t containing the IP string
 * @param char* _argument Argument in form "ADDRESS:PORT",
 *  		  like "1.2.3.4:5566"
 * @return buf_t* String buffer containing ADDRESS part of the
 *  	   argument as a string; NULL on an error
 */
static buf_t *pepa_parse_ip_string_get_ip(const char *_argument)
{
	char   *colon_ptr   = NULL;
	char   *argument    = NULL;
	size_t argument_len = 0;

	TESTP_ASSERT(_argument, "NULL argument");

	argument = strdup(_argument);
	if (NULL == argument) {
		DE("Can not duplicate argument");
		PEPA_TRY_ABORT();
		return NULL;
	}

	argument_len = strlen(argument);
	if (argument_len < 1) {
		DE("Wrong string, can not calculate len\n");
		PEPA_TRY_ABORT();
		free(argument);
		PEPA_TRY_ABORT();
		return NULL;
	}

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		DE("Can not find ':' between IP and PORT: IP:PORT\n");
		PEPA_TRY_ABORT();
		free(argument);
		return NULL;
	}

	/* We set null terminator instead ':' in the string "1.2.3.4:5566" */
	*colon_ptr = '\0';

	/* Add the IP address string into buf_t buffer */
	return buf_from_string(argument, argument_len);
}

int pepa_parse_arguments(int argi, char *argv[])
{
	/* IP address to connect to a server */
	//ip_port_t *ip_prev  = NULL;
	pepa_core_t *core = pepa_get_core();

	/* We need at least 6 params : -- addr "address:port" -i "input_file" -o "output_file" */
	if (argi < 6) {
		printf("ERROR: At least 3 arguments expected: SHVA, IN, OUT\n");
		pepa_show_help();
		exit(0);
	}

	/* Long options. Address should be given in form addr:port*/
	static struct option long_options[] = {
		/* These options set a flag. */
		{"help",             no_argument,            0, 'h'},
		{"shva",             required_argument,      0, 's'},
		{"out",              required_argument,      0, 'o'},
		{"in",               required_argument,      0, 'i'},
		{"inim",             required_argument,      0, 'n'},
		{"abort",            no_argument,            0, 'a'},
		{0, 0, 0, 0}
	};


	int                  opt;
	int                  option_index   = 0;
	while ((opt = getopt_long(argi, argv, "s:o:i:n:ha", long_options, &option_index)) != -1) {
		switch (opt) {
		case 's': /* SHVA Server address to connect to */
			core->shva_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			core->shva_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			DD("SHVA Addr OK: |%s| : |%d|\n", core->shva_thread.ip_string->data, core->shva_thread.port_int);
			break;
		case 'o': /* Output socket where packets from SHVA should be transfered */
			core->out_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			core->out_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			DD("OUT Addr OK: |%s| : |%d|\n", core->out_thread.ip_string->data, core->out_thread.port_int);
			break;
		case 'i': /* Input socket - read and send to SHVA */
			core->in_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			core->in_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			DD("OUT Addr OK: |%s| : |%d|\n", core->in_thread.ip_string->data, core->in_thread.port_int);
			break;
		case 'n':
		{
			int err;
			core->in_thread.clients = pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				DE("Could not parse number of client: %s\n", optarg);
				abort();
			}
			DD("Number of client of IN socket: %d\n", core->in_thread.clients);
		}
			break;
		case 'a':
			/* Set abort flag*/
			core->abort_flag = 1;
			break;
		case 'h': /* Show help */
			pepa_show_help();
			break;
		default:
			printf("Unknown argument: %c\n", opt);
			pepa_show_help();
			return -1;
		}
	}

	/* Check that all arguments are parsed and all arguments are provided */
	if (NULL == core->shva_thread.ip_string || core->shva_thread.port_int < 1) {
		DE("SHVA config is missed or incomplete\n");
		PEPA_TRY_ABORT();
		return -1;
	}

	if (NULL == core->out_thread.ip_string || core->out_thread.port_int < 1) {
		DE("OUT config is missed or incomplete\n");
		PEPA_TRY_ABORT();
		return -1;
	}

	if (NULL == core->in_thread.ip_string || core->in_thread.port_int < 1) {
		DE("IN config is missed or incomplete\n");
		PEPA_TRY_ABORT();
		return -1;
	}

	return PEPA_ERR_OK;
}

