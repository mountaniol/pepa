#include "pepa_parser.h"
#include "pepa_core.h"
#include "buf_t/buf_t.h"

static ip_port2_t *pepa_ip_port_t_alloc(void)
{
	ip_port2_t *ip = malloc(sizeof(ip_port_t));
	if (NULL == ip) {
		DE("Can't allocate\n");
		return (NULL);
	}

	memset(ip, 0, sizeof(ip_port_t));
	ip->ip = buf_string(IP_LEN);
	if (NULL == ip->ip) {
		DE("Could not allocate buf_t\n");
		abort();
	}
	return (ip);
}

static void pepa_ip_port_t_release(ip_port2_t *ip)
{
	if (NULL == ip) {
		DE("Arg is NULL\n");
		return;
	}

	if (ip->ip) {
		buf_free(ip->ip);
	}

	/* Secure way: clear memory before release it */
	memset(ip, 0, sizeof(ip_port_t));
	free(ip);
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
		return -1;
	}

	if (endptr == s) {
		DE("Invalid input %s\n", s);
		*err = 1;
		return -2;
	}

	if ((size_t)(endptr - s) != strlen(s)) {
		DE("Only part of the string '%s' converted: strlen = %zd, converted %zd\n", s, strlen(s), (size_t)(endptr - s));
		*err = 1;
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
	int rc;
	int       _err       = 0;
	char      *colon_ptr = NULL;

	TESTP_ASSERT(argument, "NULL argument");
	TESTP_ASSERT(ip, "ip is NULL");

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		DE("Can not find : between IP and PORT: IP:PORT\n");
		return -1;
	}

	int port = pepa_string_to_int_strict(colon_ptr + 1, &_err);

	if (_err) {
		DE("Can't convert port value from string to int: %s\n", colon_ptr);
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
	int rc;
	int       _err       = 0;
	char      *colon_ptr = NULL;
	char *argument = NULL;
	size_t argument_len = 0;

	TESTP_ASSERT(_argument, "NULL argument");
	TESTP_ASSERT(ip, "ip is NULL");

	argument = strdup(_argument);
	if (NULL == argument) {
		DE("Can not duplicate argument");
		return NULL;
	}

	argument_len = strlen(argument);
	if (argument_len < 1) {
		DE("Wrong string, can not calculate len\n");
		free(argument);
		return NULL;
	}

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		DE("Can not find ':' between IP and PORT: IP:PORT\n");
		free(argument);
		return NULL;
	}

	/* We set null terminator instead ':' in the string "1.2.3.4:5566" */
	*colon_ptr = '\0';

	/* Add the IP address string into buf_t buffer */
	return buf_from_string(argument, argument_len);
}

/* This parses a string containing an IP:PORT, create and returns a struct */
static ip_port2_t *pepa_parse_ip_string(ip_port2_t *ip, char *argument)
{
	int rc;
	int       _err       = 0;
	char      *colon_ptr = NULL;

	TESTP_ASSERT(argument, "NULL argument");
	TESTP_ASSERT(ip, "ip is NULL");

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		DE("Can not find : between IP and PORT: IP:PORT\n");
		pepa_ip_port_t_release(ip);
		return NULL;
	}

	/* We set null terminator instead ':' in the string "1.2.3.4:5566" */
	*colon_ptr = '\0';

	/* Add the IP address string into buf_t buffer */
	rc = buf_add(ip->ip, argument, strlen(argument));
	if (BUFT_OK != rc) {
		DE("Could not add IP string into buf_t\n");
		pepa_ip_port_t_release(ip);
		return NULL;
	}

	ip->port = pepa_string_to_int_strict(colon_ptr + 1, &_err);

	if (_err) {
		DE("Can't convert port value from string to int: %s\n", colon_ptr);
		pepa_ip_port_t_release(ip);
		return NULL;
	}

	return ip;
}

pepa_config_t *parse_arguments(int argi, char *argv[])
{
	/* IP address to connect to a server */
	//ip_port_t *ip_prev  = NULL;
	ip_port2_t *ip  = NULL;
	pepa_core_t *core = pepa_get_core();

	/* We need at least 6 params : -- addr "address:port" -i "input_file" -o "output_file" */
	if (argi < 6) {
		printf("ERROR: At least 3 arguments expected:\n");
		pepa_show_help();
		exit(0);
	}

	/* Long options. Address should be given in form addr:port*/
	static struct option long_options[] = {
		/* These options set a flag. */
		{"help",             no_argument,            0, 'h'},
		{"shva",             required_argument,      0, 's'},
		{"out",              required_argument,       0, 'o'},
		{"in",               required_argument,      0, 'i'},
		{"inim",             required_argument,      0, 'n'},
		{0, 0, 0, 0}
	};


	int                  opt;
	int                  option_index   = 0;
	while ((opt = getopt_long(argi, argv, ":a:o:i:n:h", long_options, &option_index)) != -1) {
		switch (opt) {
		case 's': /* SHVA Server address to connect to */
			core->shva_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			core->shva_thread.port = pepa_parse_ip_string_get_port(optarg);
			DD("SHVA Addr OK: |%s| : |%d|\n", core->shva_thread.ip_string, core->shva_thread.port);
			break;
		case 'o': /* Output socket where packets from SHVA should be transfered */
			core->out_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			core->out_thread.port = pepa_parse_ip_string_get_port(optarg);
			DD("OUT Addr OK: |%s| : |%d|\n", core->out_thread.ip_string, core->out_thread.port);
			break;
		case 'i': /* Input socket - read and send to SHVA */
			core->in_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			core->in_thread.port = pepa_parse_ip_string_get_port(optarg);
			DD("OUT Addr OK: |%s| : |%d|\n", core->in_thread.ip_string, core->in_thread.port);
			break;
		case 'h': /* Show help */
			pepa_show_help();
			break;
		default:
			printf("Unknown argument: %c\n", opt);
			pepa_show_help();
			exit(1);
		}
	}
}

