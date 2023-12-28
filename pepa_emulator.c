#define _GNU_SOURCE
#include <pthread.h>
#include <sys/param.h>
#include <unistd.h> /* For read() */
#include <sys/epoll.h>
#include <errno.h>

#include "buf_t/buf_t.h"
#include "pepa_config.h"
#include "pepa_core.h"
#include "slog/src/slog.h"
#include "pepa_errors.h"
#include "pepa_parser.h"
#include "pepa_socket_common.h"
#include "pepa_state_machine.h"

#define SHUTDOWN_DIVIDER (100003573)
#define SHOULD_EMULATE_DISCONNECT() (0 == (rand() % SHUTDOWN_DIVIDER))
#define RX_TX_PRINT_DIVIDER (1000000)

int        shva_to_out     = 0;
int        in_to_shva      = 0;

			#define PEPA_MIN(a,b) ((a<b) ? a : b )
const char *lorem_ipsum    = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.\0";
uint64_t   lorem_ipsum_len = 0;

static void close_emulatior(void)
{
	pepa_core_t *core = pepa_get_core();
	/* Stop threads */
	slog_debug_l("Starting EMU clean");
	pthread_cancel(core->out_thread.thread_id);
	pthread_cancel(core->in_thread.thread_id);
	pthread_cancel(core->shva_thread.thread_id);
	slog_debug_l("Finish EMU clean");
}

/* Catch Signal Handler functio */
static void signal_callback_handler(int signum, __attribute__((unused)) siginfo_t *info, __attribute__((unused))void *extra)
{
	printf("Caught signal %d\n", signum);
	if (signum == SIGINT) {
		printf("Caught signal SIGINT: %d\n", signum);
		//pepa_back_to_disconnected_state_new();
		close_emulatior();
		exit(0);
	}
}

void emu_set_int_signal_handler(void)
{
	struct sigaction action;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = signal_callback_handler;
	sigaction(SIGINT, &action, NULL);
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
		slog_fatal_l("Wrong buffer size: expected %ld buf it is %lu", head->buf_len, buf->used);
	}
}

void pepa_emulator_disconnect_mes(const char *name)
{
	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_note_l("EMU: Emulating %s disconnect", name);
	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
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
		slog_fatal_l("Could not allocate boof room: %s", buf_error_code_to_string(rc));
		abort();
	}

	head.counter = head_counter();
	head.buf_len = buffer_size;
	rc = buf_add(buf, (const char *)&head, sizeof(head));
	if (rc < 0) {
		slog_fatal_l("Failed to add to buf_t");
	}

	//slog_note_l("Buf allocated room");

	uint64_t rest = buffer_size - sizeof(buf_head_t);

	//slog_note_l("Starting copying into buf");

	while (rest > 0) {
		uint64_t to_copy_size = PEPA_MIN(lorem_ipsum_len, rest);
		int      rc           = buf_add(buf, lorem_ipsum, to_copy_size);

		if (rc != BUFT_OK) {
			slog_fatal_l("Could not create text buffer: %s", buf_error_code_to_string(rc));
			return -PEPA_ERR_BUF_ALLOCATION;
		}
		rest -= to_copy_size;
	}
	//slog_note_l("Finished copying into buf");

	return PEPA_ERR_OK;
}

buf_t *pepa_emulator_generate_buffer(uint64_t buffer_size)
{
	int   rc;
	buf_t *buf = buf_new(buffer_size);
	if (PEPA_ERR_OK == pepa_emulator_generate_buffer_buf(buf, buffer_size)) {
		return buf;
	}

	slog_warn_l("Can not generate buf");
	rc = buf_free(buf);
	if (BUFT_OK != rc) {
		slog_warn_l("Can not free buffer: %s", buf_error_code_to_string(rc));
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
			slog_error_l("Emu OUT: Could not connect to OUT (returned %d); |%s| ; waiting...", sock, strerror(errno));
			sleep(5);
		}
	} while (sock < 0);

	slog_debug_l("Established connection to OUT: %d", sock);
	return sock;
}

void pepa_emulator_out_thread_cleanup(__attribute__((unused))void *arg)
{
	int         *event_fd = (int *)arg;
	pepa_core_t *core     = pepa_get_core();
	//pepa_socket_close_out_write(core);
	//pepa_socket_close_out_listen(core);
	pepa_socket_close(core->sockets.out_write, "EMU OUT");
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

	emu_set_int_signal_handler();

	pthread_cleanup_push(pepa_emulator_out_thread_cleanup, (void *)&epoll_fd);

	/* In this thread we read from socket as fast as we can */

	buf_t       *buf        = buf_new(2048);

	do {
		//int                sock      = out_start_connection();
		core->sockets.out_write      = out_start_connection();

		struct epoll_event events[2];
		epoll_fd  = epoll_create1(EPOLL_CLOEXEC);

		if (0 != epoll_ctl_add(epoll_fd, core->sockets.out_write, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
			slog_warn_l("    OUT: Tried to add sock fd = %d and failed", core->sockets.out_write);
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
				slog_warn_l("    OUT: error on wait: %s", strerror(err));
				goto closeit;
			}

			for (i = 0; i < event_count; i++) {
				if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
					slog_info_l("    OUT: The remote disconnected: %s", strerror(err));
					goto closeit;
				}

				/* Read from socket */
				if (events[i].events & EPOLLIN) {
					do {
						reads++;
						rc = read(core->sockets.out_write, buf->data, buf->room);
						if (rc < 0) {
							slog_warn_l("    OUT: Read/Write op between sockets failure: %s", strerror(errno));
							goto closeit;
						}
						rx += rc;

						if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
							slog_debug_l("     OUT: %-7lu reads, bytes: %-7lu, Kb: %-7lu", reads, rx, (rx / 1024));
						}

					} while (rc == buf->room);

					continue;
				} /* for (i = 0; i < event_count; i++) */
			}

			/* Sometimes emulate broken connection: break the loop, then the socket will be closed */
			if (SHOULD_EMULATE_DISCONNECT()) {
				//slog_debug_l("OUT      : EMULATING DISCONNECT");
				pepa_emulator_disconnect_mes("OUT");
				goto closeit;
			}

		} while (1); /* epoll loop */
	closeit:
		close(epoll_fd);
		pepa_socket_close_out_write(core);
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
	int         rc          = -1;
	pepa_core_t *core       = pepa_get_core();
	int         event_count;
	int         i;

	emu_set_int_signal_handler();

	uint64_t    reads       = 0;
	uint64_t    tx          = 0;

	slog_debug("#############################################");
	slog_debug("##       THREAD <SHVA READER> IS STARTED   ##");
	slog_debug("#############################################");

	/* In this thread we read from socket as fast as we can */

	buf_t              *buf       = buf_new(2048);

	struct epoll_event events[20];
	int                epoll_fd   = epoll_create1(EPOLL_CLOEXEC);

	if (0 != epoll_ctl_add(epoll_fd, core->sockets.shva_rw, (EPOLLIN | EPOLLRDHUP | EPOLLHUP))) {
		slog_warn_l("SHVA READ: Tried to add shva fd = %d and failed", core->sockets.shva_rw);
		pthread_exit(NULL);
	}

	do {
		event_count = epoll_wait(epoll_fd, events, 20, 10);
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
			slog_warn_l("SHVA READ: error on wait: %s", strerror(err));
			close(epoll_fd);
			pthread_exit(NULL);
		}

		for (i = 0; i < event_count; i++) {
			if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
				slog_info_l("SHVA READ: THe remote disconnected: %s", strerror(err));
				close(epoll_fd);
				pthread_exit(NULL);
			}

			/* Read from socket */
			if (events[i].events & EPOLLIN) {
				do {
					reads++;
					rc = read(core->sockets.shva_rw, buf->data, buf->room);
					if (rc < 0) {
						slog_warn_l("SHVA READ: Read/Write op between sockets failure: %s", strerror(errno));
						close(epoll_fd);
						pthread_exit(NULL);
					}
					tx += rc;
					if (0 == (reads % RX_TX_PRINT_DIVIDER)) {
						slog_debug_l("SHVA READ: %-7lu reads, bytes: %-7lu, Kb: %-7lu", reads, tx, (tx / 1024));
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

	emu_set_int_signal_handler();

	slog_debug("#############################################");
	slog_debug("##       THREAD <SHVA WRITER> IS STARTED   ##");
	slog_debug("#############################################");

	/* In this thread we read from socket as fast as we can */

	buf_t       *buf   = buf_new(2048);

	do {
		if (0 != pepa_emulator_generate_buffer_buf(buf, (rand() % 750) + 16)) {
			slog_fatal_l("SHVA WRITE: Can't generate buf");
			pthread_exit(NULL);
		}

		// slog_note_l("SHVA WRITE: : Trying to write");
		rc = write(core->sockets.shva_rw, buf->data, buf->used);
		writes++;

		if (rc < 0) {
			slog_warn_l("SHVA WRITE: : Could not send buffer to SHVA, error: %s", strerror(errno));
			pthread_exit(NULL);
		}

		if (0 == rc) {
			slog_warn_l("SHVA WRITE: Send 0 bytes to SHVA, error: %s", strerror(errno));
			usleep(100000);
			continue;
		}

		//slog_debug_l("SHVA WRITE: ~~~~>>> Written %d bytes", rc);
		rx += rc;

		if (0 == (writes % RX_TX_PRINT_DIVIDER)) {
			slog_debug_l("SHVA WRITE: %-7lu reads, bytes: %-7lu, Kb: %-7lu", writes, rx, (rx / 1024));
		}

	} while (1); /* Generating and sending data */
	pthread_exit(NULL);
}

void pepa_emulator_shva_thread_cleanup(__attribute__((unused))void *arg)
{
	int         *sock_listen = (int *)arg;
	pepa_core_t *core        = pepa_get_core();
	int         rc           = pepa_socket_shutdown_and_close(*sock_listen, "EMU SHVA");
	if (PEPA_ERR_OK != rc) {
		slog_error_l("Could not close listening socket");
	}
	pepa_socket_close_shva_rw(core);
	//pepa_socket_shutdown_and_close(core->sockets.shva_rw, "SHVA SERVER");
}

#define EVENTS_NUM (10)
/* Create 1 read/write listening socket to emulate SHVA server */
void *pepa_emulator_shva_thread(__attribute__((unused))void *arg)
{
	pthread_t          shva_reader;
	pthread_t          shva_writer;
	int                rc                 = -1;
	pepa_core_t        *core              = pepa_get_core();
	struct sockaddr_in s_addr;
	int                sock_listen        = -1;
	// int                sock_rw     = -1;

	emu_set_int_signal_handler();

	struct epoll_event events[EVENTS_NUM];

	int                epoll_fd           = epoll_create1(EPOLL_CLOEXEC);

	pthread_cleanup_push(pepa_emulator_shva_thread_cleanup, &sock_listen);

	do {
		do {
			slog_note_l("Emu SHVA: OPEN LISTENING SOCKET");
			sock_listen = pepa_open_listening_socket(&s_addr, core->shva_thread.ip_string, core->shva_thread.port_int, 1, __func__);
			if (sock_listen < 0) {
				slog_note_l("Emu SHVA: Could not open listening socket, waiting...");
				usleep(1000);
			}
		} while (sock_listen < 0); /* Opening listening soket */

		slog_note_l("Emu SHVA: Opened listening socket");

		socklen_t addrlen      = sizeof(struct sockaddr);

		if (0 != epoll_ctl_add(epoll_fd, sock_listen, EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
			close(epoll_fd);
			slog_fatal_l("Emu SHVA: Could not add listening socket to epoll");
			pthread_exit(NULL);
		}

		do {
			int i;
			int event_count = epoll_wait(epoll_fd, events, EVENTS_NUM, 100);

			/* No events, exited by timeout */
			if (0 == event_count) {

				/* Emulate socket closing */
				if (SHOULD_EMULATE_DISCONNECT()) {
					// slog_debug_l("SHVA: EMULATING DISCONNECT");
					pepa_emulator_disconnect_mes("SHVA");
					goto reset;
				}
			}

			/* Interrupted by a signal */
			if (event_count < 0 && EINTR == errno) {
				continue;
			}
			/* An error happened, we just terminate the thread */
			if (event_count < 0) {
				slog_fatal_l("EMU SHVA: error on wait: %s", strerror(errno));
				close(epoll_fd);
				pepa_state_out_set(core, PEPA_ST_FAIL);
				pthread_exit(NULL);
			}


			for (i = 0; i < event_count; i++) {
				/* The listening socket is disconnected */
				if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
					goto reset;
				}

				if (sock_listen == events[i].data.fd) {
					slog_warn_l("SHVA: Listening socket: got connection");


					core->sockets.shva_rw = accept(sock_listen, &s_addr, &addrlen);
					slog_note_l("Emu SHVA: EXITED FROM ACCEPTING");
					if (core->sockets.shva_rw < 0) {
						slog_error_l("Emu SHVA: Could not accept: %s", strerror(errno));
						core->sockets.shva_rw = -1;
						continue;
					}

					struct timeval time_out;
					time_out.tv_sec = 3;
					time_out.tv_usec = 0;

					if (0 != setsockopt(core->sockets.shva_rw, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out))) {
						slog_debug_l("[from %s] tsetsockopt function has a problem", "EMU SHVA", strerror(errno));
					}
					if (0 != setsockopt(core->sockets.shva_rw, SOL_SOCKET, SO_SNDTIMEO, (char *)&time_out, sizeof(time_out))) {
						slog_debug_l("[from %s] tsetsockopt function has a problem", "EMU SHVA", strerror(errno));
					}


					/* Start read/write threads */
					rc = pthread_create(&shva_writer, NULL, pepa_emulator_shva_writer_thread, NULL);
					if (rc < 0) {
						slog_fatal_l("Could not create SHVA READ thread");
						pthread_exit(NULL);
					}

					rc = pthread_create(&shva_reader, NULL, pepa_emulator_shva_reader_thread, NULL);
					if (rc < 0) {
						slog_fatal_l("Could not create SHVA WRITE thread");
						pthread_exit(NULL);
					}
				}
			}

		} while (1);

		/* Emulate broken connection */
	reset:
		pthread_cancel(shva_reader);
		pthread_cancel(shva_writer);

		/* Close rw socket */
		pepa_socket_close_shva_rw(core);
		rc = pepa_socket_shutdown_and_close(sock_listen, "EMU");
		if (PEPA_ERR_OK != rc) {
			slog_error_l("Could not close listening socket");
		}
		sleep(5);
	} while (1); /* Opening connection and acceptiny */

/* Now we can start send and recv */

	pthread_cleanup_pop(0);
	pthread_exit(NULL);
}

int         in_start_connection(void)
{
	pepa_core_t *core = pepa_get_core();
	int         sock;
	do {
		sock = pepa_open_connection_to_server(core->in_thread.ip_string->data, core->in_thread.port_int, __func__);
		if (sock < 0) {
			slog_note_l("Emu IN: Could not connect to IN; waiting...");
			sleep(5);
		}
	} while (sock < 0);
	return sock;
}

void pepa_emulator_in_thread_cleanup(__attribute__((unused))void *arg)
{
	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
	slog_note("$$$$$$$    IN_FORWARD CLEANUP            $$$$$$$$$");
	pepa_core_t *core  = pepa_get_core();
	pepa_socket_close_in_listen(core);
	slog_note("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
}

/* Create 1 read/write listening socket to emulate SHVA server */
void *pepa_emulator_in_thread(__attribute__((unused))void *arg)
{
	int         rc     = -1;
	pepa_core_t *core  = pepa_get_core();

	uint64_t    writes = 0;
	uint64_t    rx     = 0;

	emu_set_int_signal_handler();

	pthread_cleanup_push(pepa_emulator_in_thread_cleanup, NULL);

	/* In this thread we read from socket as fast as we can */

	buf_t       *buf   = buf_new(2048);

	do {
		/* Note: the in_start_connection() function can not fail; it blocking until connection opened */
		core->sockets.in_listen = in_start_connection();
		slog_warn_l("     IN: Opened connectuin to IN: fd = %d", core->sockets.in_listen);

		do {
			if (0 != pepa_emulator_generate_buffer_buf(buf, (rand() % 750) + 16)) {
				slog_fatal_l("     IN: Can't generate buf");
				break;
			}

			// slog_note_l("     IN: Trying to write");
			rc = write(core->sockets.in_listen, buf->data, buf->used);
			writes++;

			if (rc < 0) {
				slog_warn_l("     IN: Could not send buffer to SHVA, error: %s", strerror(errno));
				break;
			}

			if (0 == rc) {
				slog_warn_l("     IN: Send 0 bytes to SHVA, error: %s", strerror(errno));
				usleep(10000);
				break;
			}

			//slog_debug_l("SHVA WRITE: ~~~~>>> Written %d bytes", rc);
			rx += rc;

			if (0 == (writes % RX_TX_PRINT_DIVIDER)) {
				slog_debug_l("     IN: %-7lu writes, bytes: %-7lu, Kb: %-7lu", writes, rx, (rx / 1024));
			}

			/* Emulate socket closing */
			if (SHOULD_EMULATE_DISCONNECT()) {
				pepa_emulator_disconnect_mes("IN");
				break;
			}

		} while (1); /* Generating and sending data */

		slog_warn_l("     IN: Closing connection");
		close(core->sockets.in_listen);
		core->sockets.in_listen = -1;
		sleep(5);
	} while (1);

	pthread_cleanup_pop(0);
	pthread_exit(NULL);
}

int main(int argi, char *argv[])
{
	slog_init("EMU", SLOG_FLAGS_ALL, 0);
	pepa_core_init();
	pepa_core_t *core = pepa_get_core();

	int         rc    = pepa_parse_arguments(argi, argv);
	if (rc < 0) {
		slog_fatal_l("Could not parse");
		return rc;
	}

	rc = pepa_config_slogger(core);
	if (PEPA_ERR_OK != rc) {
		slog_error_l("Could not init slogger");
		return rc;
	}

	lorem_ipsum_len = strlen(lorem_ipsum);

	emu_set_int_signal_handler();

	if (rc != 0) {
		sloge("Can not install pthread ignore sig");
		exit(5);
	}

	srand(17);
	/* Somethime random can return predictable value in the beginning; we skip it */
	rc = rand();
	rc = rand();
	rc = rand();
	rc = rand();
	rc = rand();

	if (NULL != core->out_thread.ip_string) {
		slog_info_l("Starting OUT thread");
		rc = pthread_create(&core->out_thread.thread_id, NULL, pepa_emulator_out_thread, NULL);
		if (0 == rc) {
			slog_note_l("SHVA thread is started");
		} else {
			pepa_parse_pthread_create_error(rc);
			return -PEPA_ERR_THREAD_CANNOT_CREATE;
		}
	}

	usleep(500000);

	if (NULL != core->shva_thread.ip_string) {
		slog_info_l("Starting SHVA thread");
		rc = pthread_create(&core->shva_thread.thread_id, NULL, pepa_emulator_shva_thread, NULL);
		if (0 == rc) {
			slog_note_l("SHVA thread is started");
		} else {
			pepa_parse_pthread_create_error(rc);
			return -PEPA_ERR_THREAD_CANNOT_CREATE;
		}
	}

	usleep(500000);

	if (NULL != core->in_thread.ip_string) {
		slog_info_l("Starting IN thread");
		rc = pthread_create(&core->in_thread.thread_id, NULL, pepa_emulator_in_thread, NULL);
		if (0 == rc) {
			slog_note_l("SHVA thread is started");
		} else {
			pepa_parse_pthread_create_error(rc);
			return -PEPA_ERR_THREAD_CANNOT_CREATE;
		}
	}

	while (1) {
		sleep(60);
	}
}


