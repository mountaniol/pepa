#define _GNU_SOURCE
#include <arpa/inet.h>
//#include <assert.h>
//#include <errno.h>
//#include <fcntl.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h> /* For eventfd */
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include <unistd.h> /* For read() */
#include <semaphore.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h> /* For read() */
#include <sys/eventfd.h> /* For eventfd */
/* THe next two are needed for send() */
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>


#include "buf_t/buf_t.h"
#include "buf_t/se_debug.h"
#include "pepa_config.h"
#include "pepa_core.h"
#include "pepa_debug.h"
#include "pepa_errors.h"
#include "pepa_ip_struct.h"
#include "pepa_parser.h"
//#include "pepa_socket.h"
#include "pepa_socket_common.h"
#include "pepa_state_machine.h"
#include "pepa_version.h"

#define SHUTDOWN_DIVIDER (1077711)
#define SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHUTDOWN_DIVIDER))

int        shva_to_out     = 0;
int        in_to_shva      = 0;

			#define PEPA_MIN(a,b) ((a<b) ? a : b )
const char *lorem_ipsum    = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.\0";
uint64_t   lorem_ipsum_len = 0;

/* Keep buffers sent from OUT */
buf_t      *buf_out_sent   = NULL;

/* Keep buffers sent from IN */
buf_t      *buf_in_sent    = NULL;

/* Keep buffers sent from SHVA */
buf_t      *buf_shva_sent  = NULL;


static void pepa_emilator_show_help(void)
{
	printf("Use:\n"
		   "--shva    | -s - address of SHVA server to listen in form: '1.2.3.4:7887'\n"
		   "--out     | -o - address of OUT server to connect to, in form '1.2.3.4:9779'\n"
		   "--in      | -i - address of IN server to connect, waiting for OUT stram connnection, in form '1.2.3.4:3748'\n"
		   "--inim    | -n - max number of IN clients to connect to, by default 1024\n"
		   "--abort   | -a - abort on errors, for debug\n"
		   "--bsize   | -b - size of internal buffer, in bytes; if not given, 1024 byte will be set\n"
		   "--version | -v - show version + git revision + compilation time\n"
		   "--help    | -h - show this help\n");
}

static int pepa_emulator_parse_arguments(int argi, char *argv[])
{
	pepa_core_t          *core          = pepa_get_core();
	/* Long options. Address should be given in form addr:port*/
	static struct option long_options[] = {
		/* These options set a flag. */
		{"help",             no_argument,            0, 'h'},
		{"shva",             required_argument,      0, 's'},
		{"out",              required_argument,      0, 'o'},
		{"in",               required_argument,      0, 'i'},
		{"inim",             required_argument,      0, 'n'},
		{"abort",            no_argument,            0, 'a'},
		{"bsize",            no_argument,            0, 'b'},
		{"version",          no_argument,            0, 'v'},
		{0, 0, 0, 0}
	};


	int                  opt;
	int                  option_index   = 0;
	while ((opt = getopt_long(argi, argv, "s:o:i:n:hav", long_options, &option_index)) != -1) {
		switch (opt) {
		case 's': /* SHVA Server address to connect to */
			core->shva_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->shva_thread.ip_string) {
				DE("Could not parse SHVA ip address\n");
				abort();
			}
			core->shva_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			DD("SHVA Addr OK: |%s| : |%d|\n", core->shva_thread.ip_string->data, core->shva_thread.port_int);
			break;
		case 'o': /* Output socket where packets from SHVA should be transfered */
			core->out_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->out_thread.ip_string) {
				DE("Could not parse OUT ip address\n");
				abort();
			}
			core->out_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			DD("OUT Addr OK: |%s| : |%d|\n", core->out_thread.ip_string->data, core->out_thread.port_int);
			break;
		case 'i': /* Input socket - read and send to SHVA */
			core->in_thread.ip_string = pepa_parse_ip_string_get_ip(optarg);
			if (NULL == core->in_thread.ip_string) {
				DE("Could not parse IN ip address\n");
				abort();
			}
			core->in_thread.port_int = pepa_parse_ip_string_get_port(optarg);
			DD("IN Addr OK: |%s| : |%d|\n", core->in_thread.ip_string->data, core->in_thread.port_int);
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
		case 'b':
		{
			int err;
			core->internal_buf_size = pepa_string_to_int_strict(optarg, &err);
			if (err < 0) {
				DE("Could not parse internal buffer size: %s\n", optarg);
				abort();
			}
			DD("Internal buffer size is set to: %d\n", core->internal_buf_size);
		}
			break;
		case 'a':
			/* Set abort flag*/
			core->abort_flag = 1;
			break;
		case 'h': /* Show help */
			pepa_emilator_show_help();
			exit(0);
		case 'v': /* Show help */
			pepa_print_version();
			exit(0);
		default:
			printf("Unknown argument: %c\n", opt);
			pepa_emilator_show_help();
			return -1;
		}
	}

	if (NULL == core->out_thread.ip_string) {
		DE("Not inited OUT arguments\n");
	}

	if (NULL == core->in_thread.ip_string) {
		DE("Not inited IN arguments\n");
	}

	if (NULL == core->out_thread.ip_string) {
		DE("Not inited SHVA arguments\n");
	}

	return 0;
}

int pepa_emulator_generate_buffer_buf(buf_t *buf, int64_t buffer_size)
{
	int rc = 0;
	TESTP(buf, -1);
	buf->used = 0;

	if (buf->room < buffer_size) {
		rc = buf_test_room(buf, buffer_size - buf->room);
	}
	if (BUFT_OK != rc) {
		DE("Could not allocate boof room: %s\n", buf_error_code_to_string(rc));
		abort();
	}

	DDD0("Buf allocated room\n");

	uint64_t rest = buffer_size;

	DDD0("Starting copying into buf\n");

	while (rest > 0) {
		uint64_t to_copy_size = PEPA_MIN(lorem_ipsum_len, rest);
		int      rc           = buf_add(buf, lorem_ipsum, to_copy_size);

		if (rc != BUFT_OK) {
			DE("Could not create text buffer: %s\n", buf_error_code_to_string(rc));
			return -1;
		}
		rest -= to_copy_size;
	}
	DD0("Finished copying into buf\n");

	return 0;
}

buf_t *pepa_emulator_generate_buffer(uint64_t buffer_size)
{
	int   rc;
	buf_t *buf = buf_new(buffer_size);
	if (0 == pepa_emulator_generate_buffer_buf(buf, buffer_size)) {
		return buf;
	}

	DDE("Can not generate buf\n");
	rc = buf_free(buf);
	if (BUFT_OK != rc) {
		DDE("Can not free buffer: %s\n", buf_error_code_to_string(rc));
	}
	return NULL;
}

static int pepa_emulator_try_to_read_from_fd(int fd, buf_t *buf)
{
	int            rc;
	fd_set         read_set;
	struct timeval tv;

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	/* Socket sets */
	FD_ZERO(&read_set);

	/* Read set, signaling when there is data to read */
	FD_SET(fd, &read_set);

	rc = select(fd + 1, &read_set, NULL, NULL, &tv);

	if (!FD_ISSET(fd, &read_set)) {
		DDD("No data in the socket\n");
		return (0);
	}

	rc = read(fd, buf->data, buf->room);

	if (rc < 0) {
		DDE("An error on receive, error: %s\n", strerror(errno));
		return -1;
	}

	if (0 == rc) {
		DDD("No buffer received, no error\n");
		return 0;
	}

	if (rc > 0) {
		DDD("Recevied %d bytes buffer\n", rc);
	}

	return rc;
}

static int out_start_connection(void)
{
	int         sock;
	pepa_core_t *core = pepa_get_core();

	do {
		sock = pepa_open_connection_to_server(core->out_thread.ip_string->data, core->out_thread.port_int, __func__);
		if (sock < 0) {
			DD("Emu OUT: Could not connect to OUT; |%s| waiting...\n", strerror(errno));
			sleep(5);
		}
	} while (sock < 0);
	return sock;
}

/* Connect 1 reading socket to PEPA OUT listening socket and receive packets */
void *pepa_emulator_out_thread(__attribute__((unused))void *arg)
{
	buf_t *buf = buf_new(2048);
	int   rc   = -1;
	int   sock = -1;

	while (1) {
		sock = out_start_connection();

		DD("Emu OUT: Connected to OUT\n");

		while (1) {
			rc = pepa_emulator_try_to_read_from_fd(sock, buf);
			if (rc > 0) {
				DD(" ~~~~>>> Emu OUT: Read from OUT %d bytes\n", rc);
				//sleep(rand() % 3);
				//usleep(rand() % 10000);

				/* Sometimes emulate broken connection: break the loop, then the socket will be closed */
				if (SHOULD_EMULATE_DISCONNECT()) {
					DD("OUT: EMULATING DISCONNECT\n");
					break;
				}
				continue;
			}

			if (rc == 0) {
				DE("Could not read from OUT: rc = %d, %s\n", rc, strerror(errno));
				//sleep(1);
				//continue;
				break;
			}

			if (rc <= 0) {
				DE("Could not read to OUT: rc = %d\n", rc);
			}

			close(sock);
			//pepa_close_socket(sock, "EMU");
			sock = out_start_connection();

			sleep(rand() % 7);
		}

		DD("Emulating OUT broken connection\n");
		close(sock);
		//pepa_close_socket(sock, "EMU");
		usleep(rand() % 1000000);
		sock = -1;
	}

	return NULL;
}


typedef struct {
	pthread_t shva_read_t;
	pthread_t shva_write_t;
} shva_args_t;

/* Create 1 read/write listening socket to emulate SHVA server */
void *pepa_emulator_shva_reader_thread(__attribute__((unused))void *arg)
{
	int                reinit      = 0;
	int                rc          = -1;
	pepa_core_t        *core       = pepa_get_core();
	struct sockaddr_in s_addr;
	int                sock_listen = -1;
	int                sock_rw     = -1;

	shva_args_t shargs = (shva_args_t *)arg;

	/* In this thread we read from socket as fast as we can */

	buf_t              *buf        = NULL;

	do {
		if (NULL != buf) {
			if (BUFT_OK != buf_free(buf)) {
				DE("Can not free buffer: %s\n", buf_error_code_to_string(rc));
			}
		}
		reinit = 0;
		do {
			DDD("Emu SHVA: OPEN LISTENING SOCKET\n");
			sock_listen = pepa_open_listening_socket(&s_addr, core->shva_thread.ip_string, core->shva_thread.port_int, 1, __func__);
			if (sock_listen < 0) {
				DDE("Emu SHVA: Could not open listening socket, waiting...\n");
				sleep(1);
			}
		} while (sock_listen < 0); /* Opening listening soket */

		DDD("Emu SHVA: Opened listening socket\n");

		socklen_t addrlen      = sizeof(struct sockaddr);

		do {
			DDD("Emu SHVA: ACCEPTING\n");
			sock_rw = accept(sock_listen, &s_addr, &addrlen);
			if (sock_rw < 0) {
				DE("Emu SHVA: Could not accept\n");
				sleep(1);
			}
		} while (sock_rw < 0);

		DDD("Emu SHVA: Accepted connection\n");

		buf = buf_new(2048);

		do {
			DDD0("Trying to read\n");
			rc = pepa_emulator_try_to_read_from_fd(sock_rw, buf);
			if (rc < 0) {
				DDE("SHVA: Error on receive; restart connection\n");
				break;
			}
			if (rc > 0) {
				DD("~~~~>>> Emu SHVA: Read %d bytes\n", rc);
			}

			/* Emulate writing */
			DDD0("SHVA: Entering writing\n");

			if (0 != pepa_emulator_generate_buffer_buf(buf, (rand() % 750) + 16)) {
				DE("Can't generate buf\n");
				abort();
			}

			DDD0("SHVA: A buffer generated, going to write: sock = %d, room = %ld, used = %ld\n",
			   sock_rw, buf->room, buf->used);

			DDD0("SHVA: Trying to write\n");
			rc = write(sock_rw, buf->data, buf->used);

			if (rc < 0) {
				reinit = 1;
				DDE("SHVA: Could not send buffer to SHVA, error: %s\n", strerror(errno));
				break;
			}

			if (0 == rc) {
				DDE("SHVA: Send 0 bytes to SHVA, error: %s\n", strerror(errno));
				usleep(100000);
			}

			if (rc > 0) {
				DD("~~~~>>> Emu SHVA: Written %d bytes\n", rc);
			}

			/* Emulate socket closing */
			if (SHOULD_EMULATE_DISCONNECT()) {
				DD("SHVA: EMULATING DISCONNECT\n");
				break;
			}


			if (reinit != 0) {
				break;
			}

			//usleep(rand() % 10000);
			reinit = 0;

		} while (1); /* Generating and sending data */

		/* Emulate broken connection */

		/* Close rw socket */
		close(sock_rw);
		//pepa_close_socket(sock_rw, "EMU");
		//close(sock_listen);
		pepa_socket_shutdown_and_close(sock_listen, "EMU");
		sleep(5);

	} while (1); /* Opening connection and acceptiny */

	/* Now we can start send and recv */
	return NULL;
}
/* Create 1 read/write listening socket to emulate SHVA server */
void *pepa_emulator_shva_thread(__attribute__((unused))void *arg)
{
	int                reinit      = 0;
	int                rc          = -1;
	pepa_core_t        *core       = pepa_get_core();
	struct sockaddr_in s_addr;
	int                sock_listen = -1;
	int                sock_rw     = -1;

	buf_t              *buf        = NULL;

	do {
		if (NULL != buf) {
			if (BUFT_OK != buf_free(buf)) {
				DE("Can not free buffer: %s\n", buf_error_code_to_string(rc));
			}
		}
		reinit = 0;
		do {
			DDD("Emu SHVA: OPEN LISTENING SOCKET\n");
			sock_listen = pepa_open_listening_socket(&s_addr, core->shva_thread.ip_string, core->shva_thread.port_int, 1, __func__);
			if (sock_listen < 0) {
				DDE("Emu SHVA: Could not open listening socket, waiting...\n");
				sleep(1);
			}
		} while (sock_listen < 0); /* Opening listening soket */

		DDD("Emu SHVA: Opened listening socket\n");

		socklen_t addrlen      = sizeof(struct sockaddr);

		do {
			DDD("Emu SHVA: ACCEPTING\n");
			sock_rw = accept(sock_listen, &s_addr, &addrlen);
			if (sock_rw < 0) {
				DE("Emu SHVA: Could not accept\n");
				sleep(1);
			}
		} while (sock_rw < 0);

		DDD("Emu SHVA: Accepted connection\n");

		buf = buf_new(2048);

		do {
			DDD0("Trying to read\n");
			rc = pepa_emulator_try_to_read_from_fd(sock_rw, buf);
			if (rc < 0) {
				DDE("SHVA: Error on receive; restart connection\n");
				break;
			}
			if (rc > 0) {
				DD("~~~~>>> Emu SHVA: Read %d bytes\n", rc);
			}

			/* Emulate writing */
			DDD0("SHVA: Entering writing\n");

			if (0 != pepa_emulator_generate_buffer_buf(buf, (rand() % 750) + 16)) {
				DE("Can't generate buf\n");
				abort();
			}

			DDD0("SHVA: A buffer generated, going to write: sock = %d, room = %ld, used = %ld\n",
			   sock_rw, buf->room, buf->used);

			DDD0("SHVA: Trying to write\n");
			rc = write(sock_rw, buf->data, buf->used);

			if (rc < 0) {
				reinit = 1;
				DDE("SHVA: Could not send buffer to SHVA, error: %s\n", strerror(errno));
				break;
			}

			if (0 == rc) {
				DDE("SHVA: Send 0 bytes to SHVA, error: %s\n", strerror(errno));
				usleep(100000);
			}

			if (rc > 0) {
				DD("~~~~>>> Emu SHVA: Written %d bytes\n", rc);
			}

			/* Emulate socket closing */
			if (SHOULD_EMULATE_DISCONNECT()) {
				DD("SHVA: EMULATING DISCONNECT\n");
				break;
			}


			if (reinit != 0) {
				break;
			}

			//usleep(rand() % 10000);
			reinit = 0;

		} while (1); /* Generating and sending data */

		/* Emulate broken connection */

		/* Close rw socket */
		close(sock_rw);
		//pepa_close_socket(sock_rw, "EMU");
		//close(sock_listen);
		pepa_socket_shutdown_and_close(sock_listen, "EMU");
		sleep(5);

	} while (1); /* Opening connection and acceptiny */

	/* Now we can start send and recv */
	return NULL;
}

int in_start_connection(void)
{
	pepa_core_t *core = pepa_get_core();
	int         sock;
	do {
		sock = pepa_open_connection_to_server(core->in_thread.ip_string->data, core->in_thread.port_int, __func__);
		if (sock < 0) {
			DDD("Emu IN: Could not connect to IN; waiting...\n");
			sleep(5);
		}
	} while (sock < 0);
	return sock;
}

/* Connect N writing sockets to PEPA IN listening socket and send packets */
void *pepa_emulator_in_thread(__attribute__((unused))void *arg)
{
	int rc   = 0;
	int sock = -1;

	while (1) {
		sock = in_start_connection();
		DDD0("Emu IN: Connected to IN: fd = %d\n", sock);

		while (1) {
			int to_write;

			if (0 != pepa_test_fd(sock)) {
				DDE("Socket IN is invalid\n");
				break;
			}

			buf_t *buf = pepa_emulator_generate_buffer(rand() % 1024);
			if (NULL == buf) {
				DDE("Emu IN: Could not generate buf\n");
				//usleep(rand() % 100000);
				continue;
			}

			to_write = buf->used;
			DD0("Emu IN: Generated buffer %ld len\n", buf->used);

			rc = write(sock, buf->data, buf->used);
			if (BUFT_OK != buf_free(buf)) {
				DE("Can not free buffer: %s\n", buf_error_code_to_string(rc));
			}

			buf = NULL;

			if (to_write == rc) {
				DDD("~~~~>>> Emu IN: Sent to IN %d bytes\n", rc);
				// usleep(rand() % 100000);

				/* Once a while emulate broken connection*/
				if (SHOULD_EMULATE_DISCONNECT()) {
					DDD("IN: EMULATING DISCONNECT\n");
					break;
				}
				continue;
			}

			if (rc < 0) {
				DE("Emu IN: Could not send buffer\n");
			} else {
				DE("Emu IN: Sent to buffer\n");
			}

			break;
		}

		DDD("Emu IN: Closing connection\n");
		close(sock);
		//pepa_close_socket(sock, "EMU");
		sleep(5);
	}
	return NULL;
}

#define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

/* Catch Signal Handler functio */
static void signal_callback_handler(int signum)
{
	//printf("Caught signal SIGPIPE %d\n", signum);
	if (signum == SIGINT) {
		exit(0);
	}
}

static void emu_set_sig_handler(void)
{
	sigset_t set;
	sigfillset(&set);

	int rc = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if (rc != 0) {
		handle_error_en(rc, "pthread_sigmask");
		exit(-1);
	}

	rc = sigprocmask(SIG_SETMASK, &set, NULL);
	if (rc != 0) {
		handle_error_en(rc, "process_mask");
		exit(-1);
	}

	signal(SIGPIPE, signal_callback_handler);
	signal(SIGINT, signal_callback_handler);
}

int main(int argi, char *argv[])
{
	pepa_core_init();
	pepa_core_t *core = pepa_get_core();
	int         rc    = pepa_emulator_parse_arguments(argi, argv);
	if (rc < 0) {
		DE("Could not parse\n");
		return rc;
	}

	/* Keep buffers sent from OUT */
	buf_out_sent  = buf_array(sizeof(void *), 0);

/* Keep buffers sent from IN */
	buf_in_sent   = buf_array(sizeof(void *), 0);

/* Keep buffers sent from SHVA */
	buf_shva_sent = buf_array(sizeof(void *), 0);

	lorem_ipsum_len = strlen(lorem_ipsum);

#if 0 /* SEB */
	sigset_t set;
	sigfillset(&set);
#endif
	emu_set_sig_handler();


	// rc = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if (rc != 0) {
		DD("Can not install pthread ignore sig\n");
		exit(-1);
	}

	srand(17);
	/* Somethime random can return predictable value in the beginning; we skip it */
	rc = rand();
	rc = rand();
	rc = rand();
	rc = rand();
	rc = rand();

	if (NULL != core->out_thread.ip_string) {
		DD("Starting OUT thread\n");
		rc = pthread_create(&core->out_thread.thread_id, NULL, pepa_emulator_out_thread, NULL);
		if (0 == rc) {
			DDD("SHVA thread is started\n");
		} else {
			pepa_parse_pthread_create_error(rc);
			return -1;
		}
	}

	if (NULL != core->shva_thread.ip_string) {
		DD("Starting SHVA thread\n");
		rc = pthread_create(&core->shva_thread.thread_id, NULL, pepa_emulator_shva_thread, NULL);
		if (0 == rc) {
			DDD("SHVA thread is started\n");
		} else {
			pepa_parse_pthread_create_error(rc);
			return -1;
		}
	}

	if (NULL != core->in_thread.ip_string) {
		DD("Starting IN thread\n");
		rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_emulator_in_thread, NULL);
		if (0 == rc) {
			DDD("SHVA thread is started\n");
		} else {
			pepa_parse_pthread_create_error(rc);
			return -1;
		}
	}

	while (1) {
		sleep(60);
	}
}

