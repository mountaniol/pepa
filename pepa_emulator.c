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
#include <sys/epoll.h>
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

#define SHUTDOWN_DIVIDER (100003573)
#define SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHUTDOWN_DIVIDER))
#define RX_TX_PRINT_DIVIDER (1000000)

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
			return -PEPA_ERR_ERROR_OUT_OF_RANGE;
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

	return PEPA_ERR_OK;
}

uint64_t head_counter(void)
{
	static uint64_t counter = 0;
	return counter++;
}

typedef struct {
	uint64_t counter;
	uint64_t buf_len;
} __attribute__((packed)) buf_head_t;


int pepa_emulator_buf_count(buf_t *buf)
{
	buf_head_t *head = (buf_head_t *)buf->data;
	return head->counter;
}

void pepa_emulator_buf_validate_size(buf_t *buf)
{
	buf_head_t *head = (buf_head_t *)buf->data;
	if (head->buf_len != (uint64_t)buf->used) {
		DE("Wrong buffer size: expected %ld buf it is %lu\n", head->buf_len, buf->used);
	}
}

void pepa_emulator_disconnect_mes(const char *name)
{
	DDD("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
	DDD("EMU: Emulating %s disconnect\n", name);
	DDD("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
}


int pepa_emulator_generate_buffer_buf(buf_t *buf, int64_t buffer_size)
{
	int        rc   = 0;
	buf_head_t head;
	TESTP(buf, -1);
	buf->used = 0;

	if (buf->room < buffer_size) {
		rc = buf_test_room(buf, buffer_size - buf->room);
	}
	if (BUFT_OK != rc) {
		DE("Could not allocate boof room: %s\n", buf_error_code_to_string(rc));
		abort();
	}

	head.counter = head_counter();
	head.buf_len = buffer_size;
	rc = buf_add(buf, (const char *)&head, sizeof(head));
	if (rc < 0) {
		DE("Failed to add to buf_t\n");
	}

	DDD0("Buf allocated room\n");


	uint64_t rest = buffer_size - sizeof(buf_head_t);

	DDD0("Starting copying into buf\n");

	while (rest > 0) {
		uint64_t to_copy_size = PEPA_MIN(lorem_ipsum_len, rest);
		int      rc           = buf_add(buf, lorem_ipsum, to_copy_size);

		if (rc != BUFT_OK) {
			DE("Could not create text buffer: %s\n", buf_error_code_to_string(rc));
			return -PEPA_ERR_BUF_ALLOCATION;
		}
		rest -= to_copy_size;
	}
	DD0("Finished copying into buf\n");

	return PEPA_ERR_OK;
}

buf_t *pepa_emulator_generate_buffer(uint64_t buffer_size)
{
	int   rc;
	buf_t *buf = buf_new(buffer_size);
	if (PEPA_ERR_OK == pepa_emulator_generate_buffer_buf(buf, buffer_size)) {
		return buf;
	}

	DDE("Can not generate buf\n");
	rc = buf_free(buf);
	if (BUFT_OK != rc) {
		DDE("Can not free buffer: %s\n", buf_error_code_to_string(rc));
	}
	return NULL;
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

void pepa_emulator_out_thread_cleanup(__attribute__((unused))void *arg)
{
	int         *event_fd = (int *)arg;
	pepa_core_t *core     = pepa_get_core();
	close(core->sockets.out_write);
	close(*event_fd);
}

/* Create 1 read socket to emulate OUT connection */
void *pepa_emulator_out_thread(__attribute__((unused))void *arg)
{
	int         rc          = -1;
	pepa_core_t *core       = pepa_get_core();
	int         event_count;
	int         i;

	uint64_t    reads       = 0;
	uint64_t    rx          = 0;
	int         epoll_fd;

	pthread_cleanup_push(pepa_emulator_out_thread_cleanup, (void *)&epoll_fd);

	/* In this thread we read from socket as fast as we can */

	buf_t       *buf        = buf_new(2048);

	do {
		//int                sock      = out_start_connection();
		core->sockets.out_write      = out_start_connection();

		struct epoll_event events[2];
		epoll_fd  = epoll_create1(EPOLL_CLOEXEC);

		if (0 != epoll_ctl_add(epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
			DDE("    OUT: Tried to add sock fd = %d and failed\n", core->sockets.out_write);
			goto closeit;
		}

		do {
			event_count = epoll_wait(epoll_fd, events, 1, 300000);
			int err = errno;
			/* Nothing to do, exited by timeout */
			if (0 == event_count) {
				continue;
			}

			/* Interrupted by a signal */
			if (event_count < 0 && EINTR == err) {
				continue;
			}

			if (event_count < 0) {
				DDE("    OUT: error on wait: %s\n", strerror(err));
				goto closeit;
			}

			for (i = 0; i < event_count; i++) {
				if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
					DDE("    OUT: The remote disconnected: %s\n", strerror(err));
					goto closeit;
				}

				/* Read from socket */
				if (events[i].events & EPOLLIN) {
					do {
						reads++;
						rc = read(core->sockets.out_write, buf->data, buf->room);
						if (rc < 0) {
							DDE("    OUT: Read/Write op between sockets failure: %s\n", strerror(errno));
							goto closeit;
						}
						rx += rc;
						if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
							DD("     OUT: %-7lu reads, bytes: %-7lu, Kb: %-7lu\n", reads, rx, (rx / 1024));
						}
					} while (rc == buf->room);

					continue;
				} /* for (i = 0; i < event_count; i++) */
			}

			/* Sometimes emulate broken connection: break the loop, then the socket will be closed */
			if (SHOULD_EMULATE_DISCONNECT()) {
				//DD("OUT      : EMULATING DISCONNECT\n");
				pepa_emulator_disconnect_mes("OUT");
				break;
			}

		} while (1); /* epoll loop */
	closeit:
		close(epoll_fd);
		close(core->sockets.out_write)/*__fd*/;
	} while (1);
	/* Now we can start send and recv */
	pthread_cleanup_pop(0);
	pthread_exit(NULL);;
}

typedef struct {
	pthread_t shva_read_t;
	pthread_t shva_write_t;
} shva_args_t;

/* Create 1 read/write listening socket to emulate SHVA server */
void *pepa_emulator_shva_reader_thread(__attribute__((unused))void *arg)
{
	int                rc          = -1;
	pepa_core_t        *core       = pepa_get_core();
	int                event_count;
	int                i;

	uint64_t           reads       = 0;
	uint64_t           tx          = 0;

	/* In this thread we read from socket as fast as we can */

	buf_t              *buf        = buf_new(2048);

	struct epoll_event events[2];
	int                epoll_fd    = epoll_create1(EPOLL_CLOEXEC);

	if (0 != epoll_ctl_add(epoll_fd, core->sockets.shva_rw, EPOLLIN)) {
		DDE("SHVA READ: Tried to add shva fd = %d and failed\n", core->sockets.shva_rw);
		pthread_exit(NULL);
	}

	do {
		event_count = epoll_wait(epoll_fd, events, 1, 300000);
		int err = errno;
		/* Nothing to do, exited by timeout */
		if (0 == event_count) {
			continue;
		}

		/* Interrupted by a signal */
		if (event_count < 0 && EINTR == err) {
			continue;
		}

		if (event_count < 0) {
			DDE("SHVA READ: error on wait: %s\n", strerror(err));
			close(epoll_fd);
			pthread_exit(NULL);
		}

		for (i = 0; i < event_count; i++) {
			if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
				DDE("SHVA READ: THe remote disconnected: %s\n", strerror(err));
				close(epoll_fd);
				pthread_exit(NULL);
			}

			/* Read from socket */
			if (events[i].events & EPOLLIN) {
				do {
					reads++;
					rc = read(core->sockets.shva_rw, buf->data, buf->room);
					if (rc < 0) {
						DDE("SHVA READ: Read/Write op between sockets failure: %s\n", strerror(errno));
						close(epoll_fd);
						pthread_exit(NULL);
					}
					tx += rc;
					if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
						DD("SHVA READ: %-7lu reads, bytes: %-7lu, Kb: %-7lu\n", reads, tx, (tx / 1024));
					}
				} while (rc == buf->room);

				continue;
			} /* for (i = 0; i < event_count; i++) */
		}

	} while (1);
	/* Now we can start send and recv */
	return NULL;
}


/* Create 1 read/write listening socket to emulate SHVA server */
void *pepa_emulator_shva_writer_thread(__attribute__((unused))void *arg)
{
	int         rc     = -1;
	pepa_core_t *core  = pepa_get_core();
	uint64_t    writes = 0;
	uint64_t    rx     = 0;

	/* In this thread we read from socket as fast as we can */

	buf_t       *buf   = buf_new(2048);

	do {
		if (0 != pepa_emulator_generate_buffer_buf(buf, (rand() % 750) + 16)) {
			DE("SHVA WRITE: Can't generate buf\n");
			pthread_exit(NULL);
		}

		DDD0("SHVA WRITE: : A buffer generated, going to write: sock = %d, room = %ld, used = %ld\n",
			 core->sockets.shva_rw, buf->room, buf->used);

		DDD0("SHVA WRITE: : Trying to write\n");
		rc = write(core->sockets.shva_rw, buf->data, buf->used);
		writes++;

		if (rc < 0) {
			DDE("SHVA WRITE: : Could not send buffer to SHVA, error: %s\n", strerror(errno));
			break;
		}

		if (0 == rc) {
			DDE("SHVA WRITE: Send 0 bytes to SHVA, error: %s\n", strerror(errno));
			usleep(100000);
		}

		//DD("SHVA WRITE: ~~~~>>> Written %d bytes\n", rc);
		rx += rc;

		if (0 == (writes % RX_TX_PRINT_DIVIDER)) {
			DD("SHVA WRITE: %-7lu reads, bytes: %-7lu, Kb: %-7lu\n", writes, rx, (rx / 1024));
		}

	} while (1); /* Generating and sending data */
	pthread_exit(NULL);
}

void pepa_emulator_shva_thread_cleanup(__attribute__((unused))void *arg)
{
	int         *sock_listen = (int *)arg;
	pepa_core_t *core        = pepa_get_core();
	pepa_socket_shutdown_and_close(*sock_listen, "EMU SHVA");
	close(core->sockets.shva_rw);
}

/* Create 1 read/write listening socket to emulate SHVA server */
void *pepa_emulator_shva_thread(__attribute__((unused))void *arg)
{
	pthread_t          shva_reader;
	pthread_t          shva_writer;
	int                rc          = -1;
	pepa_core_t        *core       = pepa_get_core();
	struct sockaddr_in s_addr;
	int                sock_listen = -1;
	// int                sock_rw     = -1;

	pthread_cleanup_push(pepa_emulator_shva_thread_cleanup, &sock_listen);

	do {
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
			DDD("Emu SHVA: STARTING    ACCEPTING\n");
			core->sockets.shva_rw = accept(sock_listen, &s_addr, &addrlen);
			DDD("Emu SHVA: EXITED FROM ACCEPTING\n");
			if (core->sockets.shva_rw < 0) {
				DE("Emu SHVA: Could not accept\n");
				sleep(1);
			}
		} while (core->sockets.shva_rw < 0);

		DDD("Emu SHVA: Accepted connection, fd = %d\n", core->sockets.shva_rw);

		/* Start read/write threads */
		rc = pthread_create(&shva_writer, NULL, pepa_emulator_shva_writer_thread, NULL);
		if (rc < 0) {
			DE("Could not create SHVA READ thread");
			pthread_exit(NULL);
		}

		rc = pthread_create(&shva_reader, NULL, pepa_emulator_shva_reader_thread, NULL);
		if (rc < 0) {
			DE("Could not create SHVA WRITE thread");
			pthread_exit(NULL);
		}

		/* Wait in loop, check whether the threads alive; if not, reconnect */
		do {
			if ((pthread_kill(shva_reader, 0) < 0) ||
				(pthread_kill(shva_writer, 0) < 0)) {
				pthread_cancel(shva_reader);
				pthread_cancel(shva_writer);
				break;
			}

			usleep(10000);

			/* Emulate socket closing */
			if (SHOULD_EMULATE_DISCONNECT()) {
				// DD("SHVA: EMULATING DISCONNECT\n");
				pepa_emulator_disconnect_mes("SHVA");
				break;
			}
		} while (1);

		/* Emulate broken connection */

		/* Close rw socket */
		close(core->sockets.shva_rw);
		pepa_socket_shutdown_and_close(sock_listen, "EMU");
		sleep(5);
	} while (1); /* Opening connection and acceptiny */

	/* Now we can start send and recv */

	pthread_cleanup_pop(0);
	pthread_exit(NULL);
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

void pepa_emulator_in_thread_cleanup(__attribute__((unused))void *arg)
{
	DDD("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
	DDD("$$$$$$$    IN_FORWARD CLEANUP            $$$$$$$$$\n");
	pepa_core_t *core  = pepa_get_core();
	close(core->sockets.in_listen);
	core->sockets.in_listen = -1;
	DDD("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
}

/* Create 1 read/write listening socket to emulate SHVA server */
void *pepa_emulator_in_thread(__attribute__((unused))void *arg)
{
	int         rc     = -1;
	pepa_core_t *core  = pepa_get_core();
	//int         sock;
//	int         event_count;
//	int         i;

	uint64_t    writes = 0;
	uint64_t    rx     = 0;

	pthread_cleanup_push(pepa_emulator_in_thread_cleanup, NULL);

	/* In this thread we read from socket as fast as we can */

	buf_t       *buf   = buf_new(2048);

	do {
		core->sockets.in_listen = in_start_connection();
		DDD("     IN: Connected to IN: fd = %d\n", core->sockets.in_listen);

		do {
			if (0 != pepa_emulator_generate_buffer_buf(buf, (rand() % 750) + 16)) {
				DE("     IN: Can't generate buf\n");
				break;
			}

			DDD0("     IN:    A buffer generated, going to write: sock = %d, room = %ld, used = %ld\n",
				 core->sockets.in_listen, buf->room, buf->used);

			DDD0("     IN: Trying to write\n");
			rc = write(core->sockets.in_listen, buf->data, buf->used);
			writes++;

			if (rc < 0) {
				DDE("     IN: Could not send buffer to SHVA, error: %s\n", strerror(errno));
				break;
			}

			if (0 == rc) {
				DDE("     IN: Send 0 bytes to SHVA, error: %s\n", strerror(errno));
				usleep(10000);
				break;
			}

			//DD("SHVA WRITE: ~~~~>>> Written %d bytes\n", rc);
			rx += rc;

			if (0 == (writes % RX_TX_PRINT_DIVIDER)) {
				DD("     IN: %-7lu writes, bytes: %-7lu, Kb: %-7lu\n", writes, rx, (rx / 1024));
			}

			/* Emulate socket closing */
			if (SHOULD_EMULATE_DISCONNECT()) {
				pepa_emulator_disconnect_mes("IN");
				break;
			}

		} while (1); /* Generating and sending data */

		close(core->sockets.in_listen);
		sleep(5);
	} while (1);

	pthread_cleanup_pop(0);
	pthread_exit(NULL);
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
			return -PEPA_ERR_THREAD_CANNOT_CREATE;
		}
	}

	usleep(500000);

	if (NULL != core->shva_thread.ip_string) {
		DD("Starting SHVA thread\n");
		rc = pthread_create(&core->shva_thread.thread_id, NULL, pepa_emulator_shva_thread, NULL);
		if (0 == rc) {
			DDD("SHVA thread is started\n");
		} else {
			pepa_parse_pthread_create_error(rc);
			return -PEPA_ERR_THREAD_CANNOT_CREATE;
		}
	}

	usleep(500000);

	if (NULL != core->in_thread.ip_string) {
		DD("Starting IN thread\n");
		rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_emulator_in_thread, NULL);
		if (0 == rc) {
			DDD("SHVA thread is started\n");
		} else {
			pepa_parse_pthread_create_error(rc);
			return -PEPA_ERR_THREAD_CANNOT_CREATE;
		}
	}

	while (1) {
		sleep(60);
	}
}

