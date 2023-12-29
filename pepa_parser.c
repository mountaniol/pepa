#include <errno.h>
#include <getopt.h>
#include <sys/param.h>

#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_core.h"
#include "pepa_errors.h"
#include "pepa_debug.h"
#include "pepa_parser.h"
#include "pepa_version.h"

void pepa_print_version(void)
{
	printf("pepa-ng version: %d.%d.%d\n", PEPA_VERSION_MAJOR, PEPA_VERSION_MINOR, PEPA_VERSION_PATCH);
	printf("git commmit: %s branch %s\n", PEPA_VERSION_GIT, PEPA_BRANCH_GIT);
	printf("Compiled at %s by %s@%s\n", PEPA_COMP_DATE, PEPA_USER, PEPA_HOST);
}

void pepa_show_help(void)
{
	printf("Use:\n"
		   "--shva    | -s IP:PORT - address of SHVA server to connect to in form: '1.2.3.4:7887'\n"
		   "--out     | -o IP:PORT - address of OUT listening socket, waiting for OUT stram connnection, in form '1.2.3.4:9779'\n"
		   "--in      | -i IP:PORT - address of IN  listening socket, waiting for OUT stram connnection, in form '1.2.3.4:3748'\n"
		   "--inum    | -n N - max number of IN clients, by default 1024\n"
		   "--abort   | -a - abort on errors, for debug\n"
		   "--bsize   | -b N - size of internal buffer, in bytes; if not given, 1024 byte will be set\n"
		   "--dir     | -d DIR  - Name of directory to save the log into\n"
		   "--file    | -f NAME - Name of log file to save the log into\n"
		   "--noprint | -p - DO NOT print log onto terminal, by default it will be printed\n"
		   "--log     | -l N - log level, accumulative: 0 = no log, 7 includes also {1-6}\n"
		   "          ~        0: none, 1: falal, 2: trace, 3: error, 4: debug, 5: warn, 6: info, 7: note\n"
		   "          ~        The same log level works for printing onto display and to the log file\n"
		   "--monitor | -m - Run  monitor, it will print status every 5 seconds\n"
		   "--daemon  | -w - Run as a daemon process; pid file /var/run/pepa.pid will be created\n"
		   "--version | -v - show version + git revision + compilation time\n"
		   "--help    | -h - show this help\n");
}

long int pepa_string_to_int_strict(char *s, int *err)
{
	char     *endptr;
	long int res;
	errno = 0;
	/* strtol detects '0x' and '0' prefixes */
	res = strtol(s, &endptr, 0);
	if ((errno == ERANGE && (res == LONG_MAX || res == LONG_MIN)) || (errno != 0 && res == 0)) {
		slog_fatal_l("Invalid input %s", s);
		perror("strtol");
		*err = errno;
		PEPA_TRY_ABORT();
		return -PEPA_ERR_INVALID_INPUT;
	}

	if (endptr == s) {
		slog_fatal_l("Invalid input %s", s);
		*err = 1;
		PEPA_TRY_ABORT();
		return -PEPA_ERR_INVALID_INPUT;
	}

	if ((size_t)(endptr - s) != strlen(s)) {
		slog_fatal_l("Only part of the string '%s' converted: strlen = %zd, converted %zd", s, strlen(s), (size_t)(endptr - s));
		*err = 1;
		PEPA_TRY_ABORT();
		return -PEPA_ERR_INVALID_INPUT;
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

int pepa_parse_ip_string_get_port(const char *argument)
{
	int  _err       = 0;
	char *colon_ptr = NULL;

	TESTP_ASSERT(argument, "NULL argument");

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		slog_fatal_l("Can not find : between IP and PORT: IP:PORT");
		PEPA_TRY_ABORT();
		return -PEPA_ERR_ADDRESS_FORMAT;
	}

	int port = pepa_string_to_int_strict(colon_ptr + 1, &_err);

	if (_err) {
		slog_fatal_l("Can't convert port value from string to int: %s", colon_ptr);
		PEPA_TRY_ABORT();
		return -PEPA_ERR_ADDRESS_FORMAT;
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
buf_t *pepa_parse_ip_string_get_ip(const char *_argument)
{
	char   *colon_ptr   = NULL;
	char   *argument    = NULL;
	size_t argument_len = 0;

	TESTP_ASSERT(_argument, "NULL argument");

	argument = strdup(_argument);
	if (NULL == argument) {
		slog_fatal_l("Can not duplicate argument");
		PEPA_TRY_ABORT();
		return NULL;
	}

	argument_len = strlen(argument);
	if (argument_len < 1) {
		slog_fatal_l("Wrong string, can not calculate len");
		PEPA_TRY_ABORT();
		free(argument);
		PEPA_TRY_ABORT();
		return NULL;
	}

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		slog_fatal_l("Can not find ':' between IP and PORT: IP:PORT");
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
	pepa_core_t          *core          = pepa_get_core();

	/* Long options. Address should be given in form addr:port*/
	static struct option long_options[] = {
		/* These options set a flag. */
		{"help",             no_argument,            0, 'h'},
		{"shva",             required_argument,      0, 's'},
		{"out",              required_argument,      0, 'o'},
		{"in",               required_argument,      0, 'i'},
		{"inum",             required_argument,      0, 'n'},
		{"log",              required_argument,      0, 'l'},
		{"file",             required_argument,      0, 'f'},
		{"dir",              required_argument,      0, 'd'},
		{"noprint",          no_argument,            0, 'p'},
		{"abort",            no_argument,            0, 'a'},
		{"bsize",            no_argument,            0, 'b'},
		{"monitor",          no_argument,            0, 'm'},
		{"daemon",           no_argument,            0, 'w'},
		{"version",          no_argument,            0, 'v'},
		{0, 0, 0, 0}
	};

	int                  err;
	int                  log;
	int                  opt;
	int                  option_index   = 0;
	if (argi < 2) {
		printf("No arguments provided\n");
		pepa_show_help();
		pepa_print_version();
		return -1;

	}
	while ((opt = getopt_long(argi, argv, "s:o:i:n:l:f:d:phavmw", long_options, &option_index)) != -1) {
		switch (opt) {
		case 's': /* SHVA Server address to connect to */
			core->shva_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->shva_thread.ip_string) {
				slog_fatal_l("Could not parse SHVA ip address");
				abort();
			}
			core->shva_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			slog_info_l("SHVA Addr OK: |%s| : |%d|", core->shva_thread.ip_string->data, core->shva_thread.port_int);
			break;
		case 'o': /* Output socket where packets from SHVA should be transfered */
			core->out_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->out_thread.ip_string) {
				slog_fatal_l("Could not parse OUT ip address");
				abort();
			}
			core->out_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			slog_info_l("OUT Addr OK: |%s| : |%d|", core->out_thread.ip_string->data, core->out_thread.port_int);
			break;
		case 'i': /* Input socket - read and send to SHVA */
			core->in_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->in_thread.ip_string) {
				slog_fatal_l("Could not parse IN ip address");
				abort();
			}
			core->in_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			slog_info_l("IN Addr OK: |%s| : |%d|", core->in_thread.ip_string->data, core->in_thread.port_int);
			break;
		case 'n':
		{
			core->in_thread.clients = pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				slog_fatal_l("Could not parse number of client: %s", optarg);
				abort();
			}
			slog_info_l("Number of client of IN socket: %d", core->in_thread.clients);
		}
			break;
		case 'b':
		{
			core->internal_buf_size = pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				slog_fatal_l("Could not parse internal buffer size: %s", optarg);
				abort();
			}
			slog_info_l("Internal buffer size is set to: %d", core->internal_buf_size);
		}
			break;
		case 'a':
			/* Set abort flag*/
			core->abort_flag = 1;
			break;
		case 'f':
			/* Log file name */
			core->slog_file = strndup(optarg, 1024);
			slog_info_l("Log file name is set to: %s", core->slog_file);
			break;
		case 'd':
			/* Log file directory name */
			core->slog_dir = strndup(optarg, 1024);
			slog_info_l("Log file name is set to: %s", core->slog_dir);
			break;
		case 'p':
			/* Set abort flag*/
			core->slog_print = 0;
			slog_info_l("Log display is disabled");
			break;
		case 'l':
			/* Set log level, 0-7*/
			log = pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				printf("Could not parse log level: %s\n", optarg);
				abort();
			}

			if (log < 0 || log > 7) {
				printf("Log level is invalid: given %s, must be from 0 to 7 inclusive\n", optarg);
				abort();
			}

			switch (log) {
			case 0:
				core->slog_flags = 0;
				break;
			case 1:
				core->slog_flags = SLOG_LEVEL_1;
				break;
			case 2:
				core->slog_flags = SLOG_LEVEL_2;
				break;
			case 3:
				core->slog_flags = SLOG_LEVEL_3;
				break;
			case 4:
				core->slog_flags = SLOG_LEVEL_4;
				break;
			case 5:
				core->slog_flags = SLOG_LEVEL_5;
				break;
			case 6:
				core->slog_flags = SLOG_LEVEL_6;
				break;
			case 7:
				core->slog_flags = SLOG_LEVEL_7;
				break;
			}

			break;
		case 'h': /* Show help */
			pepa_show_help();
			exit(0);
		case 'v': /* Show help */
			pepa_print_version();
			exit(0);
		case 'm':
			slog_debug_l("Asked to start MONITOR");
			core->monitor.onoff = 1;
			break;
		case 'w':
			core->daemon = 1;
			break;
		default:
			printf("Unknown argument: %c\n", opt);
			pepa_show_help();
			return -PEPA_ERR_ERROR_OUT_OF_RANGE;
		}
	}

	/* Check that all arguments are parsed and all arguments are provided */
	if (NULL == core->shva_thread.ip_string || core->shva_thread.port_int < 1) {
		slog_fatal_l("SHVA config is missed or incomplete");
		PEPA_TRY_ABORT();
		return -PEPA_ERR_INVALID_INPUT;
	}

	if (NULL == core->out_thread.ip_string || core->out_thread.port_int < 1) {
		slog_fatal_l("OUT config is missed or incomplete");
		PEPA_TRY_ABORT();
		return -PEPA_ERR_INVALID_INPUT;
	}

	if (NULL == core->in_thread.ip_string || core->in_thread.port_int < 1) {
		slog_fatal_l("IN config is missed or incomplete");
		PEPA_TRY_ABORT();
		return -PEPA_ERR_INVALID_INPUT;
	}

	return PEPA_ERR_OK;
}

int pepa_config_slogger(pepa_core_t *core)
{
	slog_config_t cfg;
	slog_config_get(&cfg);

	cfg.nTraceTid = 1;
	cfg.eDateControl = SLOG_DATE_FULL;

	if (NULL != core->slog_file) {
		cfg.nToFile = 1;
		cfg.nKeepOpen = 1;
		cfg.nFlush = 1;
		strcpy(cfg.sFileName, core->slog_file);
	} else {
		slog_note_l("No log file given");
	}

	if (NULL != core->slog_dir) {
		strcpy(cfg.sFilePath, core->slog_dir);
	}

	if (0 == core->slog_print) {
		cfg.nToScreen = 0;
	}

	slog_config_set(&cfg);
	slog_enable(SLOG_TRACE);
	//slog_init("pepa", core->slog_level, 1);
	slog_config_set(&cfg);
	slog_disable(SLOG_FLAGS_ALL);
	slog_enable(core->slog_flags);
	return PEPA_ERR_OK;
}

int pepa_config_slogger_daemon(pepa_core_t *core)
{
	slog_config_t cfg;
	slog_config_get(&cfg);

	cfg.nTraceTid = 1;
	cfg.eDateControl = SLOG_DATE_FULL;

	if (NULL != core->slog_file) {
		cfg.nToFile = 1;
		cfg.nKeepOpen = 1;
		cfg.nFlush = 1;
		strcpy(cfg.sFileName, core->slog_file);
	} else {
		slog_note_l("No log file given");
	}

	if (NULL != core->slog_dir) {
		strcpy(cfg.sFilePath, core->slog_dir);
	}

	cfg.nToScreen = 0;

	slog_config_set(&cfg);
	slog_disable(SLOG_FLAGS_ALL);
	slog_enable(core->slog_flags);
	return PEPA_ERR_OK;
}
