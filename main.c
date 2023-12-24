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
#include "pepa_config.h"
#include "pepa_ip_struct.h"
#include "pepa_core.h"
#include "pepa_errors.h"
#include "pepa_parser.h"
#include "pepa_socket_common.h"
#include "pepa_version.h"
#include "pepa_state_machine.h"

#if 0 /* SEB */
/**** GLOBAL FILE DESCRIPTORS *****/
/* File descriptor of IN file, i.e., a file to read from */
int  fd_out     = -1;
/* File descriptor of OUT file, i.e., a file write to */
int  fd_in      = -1;

/* File descriptor on an opened socket */
int  fd_sock    = -1;

char *file_name_fifo = NULL;

static ip_port_t *pepa_ip_port_t_alloc(void){
	ip_port_t *ip = malloc(sizeof(ip_port_t));
	if (NULL == ip) {
		DE("Can't allocate\n");
		return (NULL);
	}

	memset(ip, 0, sizeof(ip_port_t));
	return (ip);
}

static void pepa_ip_port_t_release(ip_port_t *ip){
	if (NULL == ip) {
		DE("Arg is NULL\n");
		return;
	}

	/* Secure way: clear memory before release it */
	memset(ip, 0, sizeof(ip_port_t));
	free(ip);
}

static void pepa_show_help(void){
	printf("Use:\n"
		   "-h - show this help\n"
		   "--addr | -a - address to connect to in form:1.2.3.4:7887\n"
		   "--out  | -o - output file, means the file pepa will write received from socket\n"
		   "--in   | -i - input file, means the file pepa will listenm read from and send to socket\n");
}

long int pepa_string_to_int_strict(char *s, int *err){
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

/* This parses a string containing an IP:PORT, create and returns a struct */
static ip_port_t *pepa_parse_ip_string(char *argument){
	ip_port_t *ip        = NULL;
	int       _err       = 0;
	char      *colon_ptr = NULL;

	TESTP_ASSERT(argument, "NULL argument");

	ip = pepa_ip_port_t_alloc();
	TESTP_ASSERT(ip, "ip is NULL");

	colon_ptr = index(argument, ':');

	if (NULL == colon_ptr) {
		DE("Can not find : between IP and PORT: IP:PORT\n");
		pepa_ip_port_t_release(ip);
		return NULL;
	}

	*colon_ptr = '\0';

	strncpy(ip->ip, argument, ((IP_LEN - 1)));
	ip->port = pepa_string_to_int_strict(colon_ptr + 1, &_err);

	if (_err) {
		DE("Can't convert port value from string to int: %s\n", colon_ptr);
		pepa_ip_port_t_release(ip);
		return NULL;
	}

	return ip;
}

/**
 * @author Sebastian Mountaniol (7/17/23)
 * @brief Open an IN file for read
 * @param char* file_name File / Pipe name to open
 * @return int File descriptor on success, NULL on an error
 * @details THe file must exists
 */
static int pepa_open_pipe_in(char *file_name){
	TESTP_ASSERT(file_name, "file_name is NULL");
	int fd = open(file_name, O_RDONLY | O_CLOEXEC);
	// int fd = open(file_name, O_RDONLY | O_NONBLOCK);
	DD("Opening FD IN\n");
	//int fd = open(file_name, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		DE(">>> Can not open file: %s", strerror(errno));
		return -1;
	}

	DD("Opened FILE IN: %s : %d\n", file_name, fd);
	return fd;
}

/**
 * @author Sebastian Mountaniol (7/17/23)
 * @brief Open a file for OUT, i.e. to write from socket 
 * @param char _file_name File name to open. 
 * @return int File descriptor or -1 on an error
 * @details FIle MUST exist, not created! Also, the content of
 *  		the file will not be trunkated, i.e. opened in "a"
 *  		mode. It is up to end user to decide either the file
 *  		should be cleaned before a writing will start
 */
static int pepa_open_file_out(char *file_name){
	TESTP_ASSERT(file_name, "file_name is NULL");
	int fd = open(file_name, O_WRONLY | O_APPEND | O_CLOEXEC);
	if (fd < 0) {
		DE(">>> Can not open file: %s", strerror(errno));
		return -1;
	}
	return fd;
}

/**
 * @author Sebastian Mountaniol (7/17/23)
 * @brief Open TCP connection to a remote server AND connect to
 * @param ip_port_t* ip  A structure containing ip (as a string)
 *  			   and a port (as an integer)
 * 
 * @return int Opened socket; a negative on an error
 * @details If the function can not open a socket, it returns
 *  		-1. If this function open a socket but cannot
 *  		connect to the remote server, it closes the socket
 *  		returns -2. If the socket opened AND connected, it
 *  		returns the socket descriptor, which is >= 0
 */
static int pepa_connect_to_shva(ip_port_t *ip){
	struct sockaddr_in s_addr;
	int                sock;

	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = (sa_family_t)AF_INET;

	DD("1\n");

	const int convert_rc = inet_pton(AF_INET, ip->ip, &s_addr.sin_addr);
	if (0 == convert_rc) {
		DE("The string is not a valid IP address: |%s|\n", ip->ip);
		return (-PEPA_ERR_CONVERT_ADDR);
	}

	DD("2\n");

	if (convert_rc < 0) {
		DE("Could not convert string addredd |%s| to binary\n", ip->ip);
		return (-PEPA_ERR_CONVERT_ADDR);
	}
	DD("3\n");

	s_addr.sin_port = htons(ip->port);

	DD("4\n");

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		DE("could not create socket\n");
		return (-PEPA_ERR_SOCKET_CREATION);
	}

	DD("5\n");

	if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
		DE("could not connect to server\n");
		close(sock);
		return (-PEPA_ERR_SOCK_CONNECT);
	}

	DD("6\n");
	return (sock);
}

/**
 * @author Sebastian Mountaniol (7/18/23)
 * @brief Read from file desctiptor, and write to another file
 *  	  descriptor 
 * @param int fd_from File descriptor to read from
 * @param int fd_to  File descriptor to write to 
 * @return int Number of processes bytes
 * @details 
 */
int pepa_copy_fd_to_fd(int fd_from, int fd_to){
	uint8_t buf[COPY_BUF_SIZE];
	int     rc_read            = 0;
	int     accum              = 0;
	do {
		rc_read = read(fd_from, buf, COPY_BUF_SIZE);

		if (rc_read < 0) {
			perror("Can not read from file descriptor: ");
			break;
		}

		DD("Copied from IN to buf: %d bytes\n", rc_read);

		if (0 == rc_read) {
			DE("Got 0 bytes, return");
			break;
		}

		const int rc_write = write(fd_to, buf, rc_read);
		if (rc_write != rc_read) {
			DE("Could not write the same amount of bytes: read %u, wrote %u\n", rc_read, rc_write);
			abort();
		}

		DD("Copied from buf to OUT: %d bytes\n", rc_write);

		accum += rc_write;


	} while (rc_read > 0);

	return accum;
}

/**
 * @author Sebastian Mountaniol (7/19/23)
 * @brief Read from FIFO, write to socket
 * @param void* arg   
 * @return void* 
 * @details 
 */
void *pepa_merry_go_round_fifo(__attribute__((unused)) void *arg){
	uint64_t       accum_to_sock   = 0;

	while (1) {
		if (fd_in > 0){
			close(fd_in);
		}

		fd_in = pepa_open_pipe_in(file_name_fifo);
		if (fd_in < 0) {
			usleep(100);
			continue;
		}

		int rc = pepa_copy_fd_to_fd(fd_in, fd_sock);
		if (rc < 0) {
			DE("Error on writing from FIFO to sock\n");
			continue;
		}

		accum_to_sock+= rc;
	} // while
}

/**
 * @author Sebastian Mountaniol (7/19/23)
 * @brief Read from socket, write to output file
 * @param void* arg   
 * @return void* 
 * @details 
 */
void *pepa_merry_go_round_sock(__attribute__((unused)) void *arg){
	fd_set         rfds;

/* Select related variables */
	struct timeval tv;
	int            retval          = -1;
	uint64_t       accum_from_sock = 0;

	while (1) {
		FD_ZERO(&rfds);
		FD_SET(fd_sock, &rfds);

		/* Wait 5 seconds */
		tv.tv_sec = 5;
		tv.tv_usec = 0;

		/* Wait a signal from the second side of the FIFO */

		DD("Going to wait on select() for 5 seconds\n");

		retval = select(fd_sock + 1, &rfds, NULL, NULL, &tv);

		/* nothing to read */
		if (retval == 0) {
			DD("select() returned 0 - contunie\n");
			continue;
		}

		/* The only case when select() returns -1 and it is legal, it is a signal;
		 * we ignore signals and continue select() */
		if ((retval == -1) && (errno == EINTR)) {
			DD("select() returned an error - interrupt\n");
			perror("select error: ");
			continue;
		}

		/* Any other case the select() returns -1, we have to stop */
		if (retval == -1) {
			perror("select() : ");
			DD("select() returned an error\n");
			abort();
		}

		/* Is there anything on socket? */
		if (FD_ISSET(fd_sock, &rfds)) {
			DD("Received something on socket\n");

			/* Read from socket, write to fd_write */
			const int32_t rc_copy = pepa_copy_fd_to_fd(fd_sock, fd_out);
			if (rc_copy < 0) {
				/* something is wrong is there */
				abort();
			}

			DD("Copied from socket to FD OUT: %d bytes\n", rc_copy);
			accum_from_sock += rc_copy;
		} // if
	} // while
}
#endif

void bye(void)
{
	pepa_core_t *core = pepa_get_core();
	pthread_cancel(core->shva_thread.thread_id);
	pthread_cancel(core->out_thread.thread_id);

	/* This function closes all descriptors,
	   frees all buffers and also frees core struct */
	pepa_core_finish();
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

static void main_set_sig_handler(void)
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
	int rc;
//	atexit(bye);

	//printf("pepa-ng version %d.%d.%d/%s\n", PEPA_VERSION_MAJOR, PEPA_VERSION_MINOR, PEPA_VERSION_PATCH, PEPA_VERSION_GIT);

	pepa_print_version();

	DDD("Going to init core\n");
	rc = pepa_core_init();
	if (PEPA_ERR_OK != rc) {
		DE("Can not init core\n");
		abort();
	}
	DDD("Core inited\n");


	DDD("Going to parse arguments\n");
	rc = pepa_parse_arguments(argi, argv);
	if (rc < 0) {
		DE(" Could not parse arguments\n");
		exit(-11);
	}

	DDD("Arguments parsed\n");

	main_set_sig_handler();
	
	DDD("Going to start threads\n");
	rc = pepa_start_threads();
	if (rc < 0) {
		DE("Could not start threads\n");
		exit(-11);
	}

	DDD("Threads are started\n");


	while (1) {
		sleep(120);
	}
	pepa_kill_all_threads();
	sleep(1);
	DD("PEPA Exit\n");
	return(0);
}

