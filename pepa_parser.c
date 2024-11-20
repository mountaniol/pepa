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
           "~~~~~~~~~~~ CONFIGURATION FILE (OPTIONAL) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
           "--config   | -C       Full name of config file, like /etc/pepa.config\n"
		   "~~~~~~~~~~~ CONNECTION ADDRESSES AND PORTS (MANDATORY) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
		   "--shva     | -s IP:PORT : Address of SHVA server to connect to in form: '1.2.3.4:7887'\n"
		   "--out      | -o IP:PORT : Address of OUT listening socket, waiting for OUT stream connection, in form '1.2.3.4:9779'\n"
		   "--in       | -i IP:PORT : Address of IN  listening socket, waiting for OUT stream connection, in form '1.2.3.4:3748'\n"
		   "\n"
		   "~~~~~~~~~~~ CONNECTION ADDITIONAL OPTIONS (OPTIONAL)~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
		   "--inum     | -n N :   Max number of IN clients, by default 1024\n"
		   "--bsize    | -b N :   Size of SEND / RECV buffer, in Kb; if not given, 64 Kb used\n"
		   "\n"
		   "~~~~~~~~~~~ PEPA MODE OPTION (OPTIONAL) ~~ ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
		   "--daemon   | -w  :    Run as a daemon process\n"
		   "           ~          WARNING: When --daemon is ON, it disables terminal printings and disbales colors\n"
		   "--pid      | -P file: Create PID file 'file'; full path should be provided. By defailt, /tmp/pepa.pid file created\n"
		   "\n"
		   "~~~~~~~~~~~ LOGGER OPTIONS (OPTIONAL) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
		   "--log      | -l N :   Logger level, accumulative: 0 = no log, 7 includes also {1-6}; Default level is 7 (everything)\n"
		   "           ~          0: none, 1: fatal, 2: trace, 3: error, 4: debug, 5: warn, 6: info, 7: note\n"
		   "           ~          The same log level works for printing onto the terminal and to the log file\n"
		   "--file     | -f :     NAME - Name of log file to save the log into\n"
		   "--dir      | -d DIR : Name of directory to save the log into\n"
		   "--noprint  | -p :     DO NOT print log onto the terminal. By default, it will be printed\n"
		   "--dump     | -u :     Dump every message into the log file\n"
		   "--color    | -c :     Enable color in terminal printings (always disabled in 'daemon' mode)\n"
		   "\n"
		   "~~~~~~~~~~~ MONITOR OPTIONS (OPTIONAL) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
		   "--monitor  | -m N   : Run  monitor. It will print status every N seconds; default output in bytes\n"
		   "--divider  | -r N|C : Monitor units: it can be a character (C) or a number (N):\n"
		   "           ~          The character argument to print units: 'b' for Bytes, 'k' for Kbytes, 'm' for Mbytes\n"
		   "           ~          The argument can be also numeric: --divider 100 to print in 100 byte units\n"
		   "           ~          Example of monitor config: '--monitor 10 --divider k' - print status every 10 sec in Kbytes\n"
		   "\n"
		   "~~~~~~~~~~~ EMULATOR OPTIONS (FOR TEST ONLY, OPTIONAL) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
		   "--emusleep | -S N :   Emulator only: Sleep N microseconds between buffer sending; 0 by default\n"
		   "--emubuf   | -B N :   Emulator only: Max size of a buffer, N must be, >= 1; It is 1024 by default\n"
		   "--emubufmin| -M N :   Emulator only: Min size of a buffer, N must be; >= 1; It is 1 by default\n"
		   "--emuin    | -I N :   Emulator only: How many IN threads it should run, minimum (and default) is 1\n"
		   "\n"
		   "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
		   "--abort    | -a :     Abort on errors for debug\n"
		   "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
		   "--version  | -v :     Show version + git revision + compilation time\n"
		   "--help     | -h :     Show this help\n");
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
		slog_fatal_l("Only part of the string '%s' converted: strlen = %zu, converted %zu", s, strlen(s), (size_t)(endptr - s));
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
	int32_t _err       = 0;
	char    *colon_ptr = NULL;

	TESTP_ASSERT(argument, "NULL argument");

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		slog_fatal_l("Can not find : between IP and PORT: IP:PORT");
		PEPA_TRY_ABORT();
		return -PEPA_ERR_ADDRESS_FORMAT;
	}

	int port = (int)pepa_string_to_int_strict(colon_ptr + 1, &_err);

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
	return buf_from_string(argument, (buf_s64_t)argument_len);
}

int pepa_parse_arguments(int argi, char *argv[])
{
	pepa_core_t          *core          = pepa_get_core();

	/* Long options. Address should be given in form addr:port*/
	static struct option long_options[] = {
		/* These options set a flag. */
		{"help",             no_argument,            0, 'h'},
		{"shva",             required_argument,      0, 's'},
        { "config",           required_argument,      0, 'C' },
		{"out",              required_argument,      0, 'o'},
		{"in",               required_argument,      0, 'i'},
		{"inum",             required_argument,      0, 'n'},
		{"log",              required_argument,      0, 'l'},
		{"file",             required_argument,      0, 'f'},
		{"dir",              required_argument,      0, 'd'},
		{"divider",          required_argument,      0, 'r'},
		{"emusleep",         required_argument,      0, 'S'},
		{"emubuf",           required_argument,      0, 'B'},
		{"emubufmin",        required_argument,      0, 'M'},
		{"emuin",            required_argument,      0, 'I'},
		{"abort",            no_argument,            0, 'a'},
		{"bsize",            no_argument,            0, 'b'},
		{"monitor",          required_argument,      0, 'm'},
		{"pid",              required_argument,      0, 'P'},
		{"daemon",           no_argument,            0, 'w'},
		{"noprint",          no_argument,            0, 'p'},
		{"color",            no_argument,            0, 'c'},
		{"dump",             no_argument,            0, 'u'},
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
		return (-PEPA_ERR_INVALID_INPUT);

	}
    while ((opt = getopt_long(argi, argv, "f:s:o:i:n:l:f:d:b:r:S:B:m:I:phavwcu", long_options, &option_index)) != -1) {
		switch (opt) {
        case 'C': /* Config file */
            core->config = strdup(optarg);
            slog_info_l("Config file: |%s|", core->config);
            break;
		case 's': /* SHVA Server address to connect to */
			core->shva_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->shva_thread.ip_string) {
				slog_fatal_l("Could not parse SHVA ip address");
				abort();
			}
			core->shva_thread.port_int = (uint16_t)pepa_parse_ip_string_get_port(optarg);
			slog_info_l("SHVA Addr OK: |%s| : |%d|", core->shva_thread.ip_string->data, core->shva_thread.port_int);
			break;
		case 'o': /* Output socket where packets from SHVA should be transfered */
			core->out_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->out_thread.ip_string) {
				slog_fatal_l("Could not parse OUT ip address");
				abort();
			}
			core->out_thread.port_int = (uint16_t)pepa_parse_ip_string_get_port(optarg);
			slog_info_l("OUT Addr OK: |%s| : |%d|", core->out_thread.ip_string->data, core->out_thread.port_int);
			break;
		case 'i': /* Input socket - read and send to SHVA */
			core->in_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->in_thread.ip_string) {
				slog_fatal_l("Could not parse IN ip address");
				abort();
			}
			core->in_thread.port_int = (uint16_t)pepa_parse_ip_string_get_port(optarg);
			slog_info_l("IN Addr OK: |%s| : |%d|", core->in_thread.ip_string->data, core->in_thread.port_int);
			break;
		case 'n':
		{
			core->in_thread.clients = (uint32_t)pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				slog_fatal_l("Could not parse number of client: %s", optarg);
				abort();
			}
			slog_info_l("Number of client of IN socket: %u", core->in_thread.clients);
		}
			break;
		case 'b':
		{
			core->internal_buf_size = (uint32_t)pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				slog_fatal_l("Could not parse internal buffer size: %s", optarg);
				abort();
			}

			/* Transform the size from Kb to bytes */
			core->internal_buf_size *= 1024;
			core->print_buf_len = core->internal_buf_size  * 4;
			slog_info_l("Internal buffer size is set to: %u (%u Kb)",
						core->internal_buf_size,
						(core->internal_buf_size / 1024));
			slog_info_l("Printing buffer size is set to: %u (%u Kb)",
						core->print_buf_len,
						(core->print_buf_len / 1024));

		}
			break;
		case 'P':
		{
			if (NULL != core->pid_file_name) {
				free(core->pid_file_name);
			}

			core->pid_file_name = strdup(optarg);
			slog_info_l("PID file is set to: %s", core->pid_file_name);
		}
			break;
		case 'r':
		{
			if (optarg[0] == 'k') {
				core->monitor_divider = 1024;
				core->monitor_divider_str[0] = 'K';
				slog_info_l("Monitor divider is set to Kbytes = %d", core->monitor_divider);
				break;
			}

			if (optarg[0] == 'm') {
				core->monitor_divider = 1024 * 1024;
				core->monitor_divider_str[0] = 'M';
				slog_info_l("Monitor divider is set to Mbytes = %d", core->monitor_divider);
				break;
			}

			if (optarg[0] == 'b') {
				core->monitor_divider = 1;
				core->monitor_divider_str[0] = 'B';
				slog_info_l("Monitor divider is set to Bytes = %d",  core->monitor_divider);
				break;
			}

			core->monitor_divider = (int)pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				slog_fatal_l("Could not parse internal buffer size: %s", optarg);
				abort();
			}

			if (core->monitor_divider <= 0) {
				slog_fatal_l("Monitor divider is invalid: %s, it must be >= 1", optarg);
				abort();
			}

			slog_info_l("Monitor divider is set to: %d", core->monitor_divider);
		}
			break;
		case 'S':
		{
			long int ret = pepa_string_to_int_strict(optarg, &err);

			if (err < 0) {
				slog_fatal_l("Could not parse emulator sleep time: %s", optarg);
				abort();
			}

			if (ret <= 0) {
				slog_fatal_l("Emulator sleep is invalid: %s, it must be >= 1", optarg);
				abort();
			}

			core->emu_timeout = (unsigned int)ret;
			slog_info_l("Emulator sleep is set to: %u", core->emu_timeout);
		}
			break;
		case 'B':
		{
			ssize_t ret = (ssize_t)pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				slog_fatal_l("Could not emulator max buffer size: %s", optarg);
				abort();
			}

			if (ret <= 0) {
				slog_fatal_l("Emulator max size is invalid: %s, it must be >= 1", optarg);
				abort();
			}

			core->emu_max_buf = (size_t)ret;

			slog_info_l("Emulator max buffer size is set to: %zu", core->emu_max_buf);
		}
			break;
		case 'M':
		{
			ssize_t ret = (ssize_t)pepa_string_to_int_strict(optarg, &err);

			if (err < 0) {
				slog_fatal_l("Could not parse emulator min buffer size: %s", optarg);
				abort();
			}

			if (ret <= 0) {
				slog_fatal_l("Emulator min size is invalid: %s, it must be >= 1", optarg);
				abort();
			}

			core->emu_min_buf = (size_t)ret;
			slog_info_l("Emulator max buffer size is set to: %zu", core->emu_min_buf);
		}
			break;
		case 'I':
		{
			ssize_t ret = (ssize_t)pepa_string_to_int_strict(optarg, &err);

			if (err < 0) {
				slog_fatal_l("Could not parse emulator IN threads: %s", optarg);
				abort();
			}

			if (ret <= 0) {
				slog_fatal_l("Emulator IN threads number is invalid: %s, it must be >= 1", optarg);
				abort();
			}

			core->emu_in_threads = (uint32_t)ret;
			slog_info_l("Emulator IN threads is set to: %u", core->emu_in_threads);
		}
			break;
		case 'm':
		{
			core->monitor_freq = (unsigned int)pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				slog_fatal_l("Could not parse monitor frequency: %s", optarg);
				abort();
			}

			if (core->monitor_freq < 1) {
				slog_fatal_l("Monitor frequency is invalid: %s, it must be >= 1", optarg);
				abort();
			}
			core->monitor.onoff = 1;
			slog_info_l("Monitor frequency is set to: %u", core->monitor_freq);
		}
			break;
		case 'a':
			/* Set abort flag*/
			core->abort_flag = YES;
			break;
		case 'f':
			/* Log file name */
			core->slog_file = strndup(optarg, 1024);
			slog_info_l("Log file name is set to: %s", core->slog_file);
			break;
		case 'd':
			/* Log file directory name */
			if (NULL != core->slog_dir) {
				free(core->slog_dir);
			}
			core->slog_dir = strndup(optarg, 1024);
			slog_info_l("Log file name is set to: %s", core->slog_dir);
			break;
		case 'p':
			/* Disable terminal printings */
			core->slog_print = NO;
			slog_info_l("Log display is disabled");
			break;
		case 'l':
			/* Set log level, 0-7*/
			log = (int)pepa_string_to_int_strict(optarg, &err);
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
			default:
				printf("Could not parse log level: %s\n", optarg);
				abort();
			}

			printf("Logger log level is set to: %d\n", log);

			break;
		case 'h': /* Show help */
			pepa_show_help();
			exit(0);
		case 'v': /* Show help */
			pepa_print_version();
			exit(0);
		case 'w':
			core->daemon = 1;
			break;
		case 'c':
			core->slog_color = YES;
			break;
		case 'u':
			core->dump_messages = YES;
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

void pepa_config_slogger(const pepa_core_t *core)
{
	slog_config_t cfg;
	slog_config_get(&cfg);

	cfg.nTraceTid = 1;
	cfg.eDateControl = SLOG_DATE_FULL;
	cfg.eColorFormat = SLOG_COLORING_DISABLE;

	if (NULL != core->slog_file) {
		cfg.nToFile = 1;
		cfg.nKeepOpen = 1;
		cfg.nFlush = 1;
		strcpy(cfg.sFileName, core->slog_file);
	} else {
		slog_note_l("No log file is given");
	}

	if (NULL != core->slog_dir) {
		strcpy(cfg.sFilePath, core->slog_dir);
	}

	if (NO == core->slog_print) {
		cfg.nToScreen = 0;
	}

	if (YES == core->slog_color) {
		cfg.eColorFormat = SLOG_COLORING_TAG;
	}

	//slog_config_set(&cfg);
	//slog_enable(SLOG_TRACE);
	slog_destroy();
	slog_init("pepa", 0, 1);
	slog_config_set(&cfg);
	slog_disable(SLOG_FLAGS_ALL);
	slog_enable(core->slog_flags);
}

void pepa_config_slogger_daemon(const pepa_core_t *core)
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
		cfg.eColorFormat = SLOG_COLORING_DISABLE;
	} else {
		slog_note_l("No log file given");
	}

	if (NULL != core->slog_dir) {
		strcpy(cfg.sFilePath, core->slog_dir);
	}

	//cfg.nToScreen = 0;

	slog_disable(SLOG_FLAGS_ALL);
	slog_config_set(&cfg);
	slog_enable(core->slog_flags);
}
