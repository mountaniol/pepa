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
#include "debug.h"

/* Max length of string IP address */
#define IP_LEN   (24)

/* Size of buffer used to copy from fd to fd*/
#define COPY_BUF_SIZE (128)

struct ip_struct {
	char ip[IP_LEN];
	int  port;
};
typedef  struct ip_struct ip_port_t;


static ip_port_t *pepa_ip_port_t_alloc(void)
{
	ip_port_t *ip = malloc(sizeof(ip_port_t));
	if (NULL == ip) {
		DE("Can't allocate\n");
		return (NULL);
	}

	memset(ip, 0, sizeof(ip_port_t));
	return (ip);
}

static void pepa_ip_port_t_release(ip_port_t *ip)
{
	if (NULL == ip) {
		DE("Arg is NULL\n");
		return;
	}

	/* Secure way: clear memory before release it */
	memset(ip, 0, sizeof(ip_port_t));
	free(ip);
}


static void pepa_show_help(void)
{
	printf("Use:\n"
		   "-h - show this help\n"
		   "--addr | -a - address to connect to in form:1.2.3.4:7887\n"
		   "--out  | -o - output file, means the file pepa will write received from socket\n"
		   "--in   | -i - input file, means the file pepa will listenm read from and send to socket\n");
}

long int pepa_string_to_int_strict(char *s, int *err)
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

	*err = 0;
	return res;
}

/* This parses a string containing an IP:PORT, create and returns a struct */
static ip_port_t *pepa_parse_ip_string(char *argument)
{
	ip_port_t *ip        = NULL;
	int       _err       = 0;
	char      *colon_ptr = NULL;

	TESTP_ASSERT(argument);

	ip = pepa_ip_port_t_alloc();
	TESTP_ASSERT(ip);

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
static int pepa_open_pipe_in(char *file_name)
{
	TESTP_ASSERT(file_name);
	int fd = open(file_name, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		DE(">>> Can not open file: %s", strerror(errno));
		return -1;
	}

	DD("Opened FILE IN: %d\n", fd);
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
static int pepa_open_file_out(char *file_name)
{
	TESTP_ASSERT(file_name);
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
static int pepa_connect_to_server(ip_port_t *ip)
{
	struct sockaddr_in s_addr;
	int                sock;

	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = (sa_family_t)AF_INET;

	DD("1\n");

	const int convert_rc = inet_pton(AF_INET, ip->ip, &s_addr.sin_addr);
	if (0 == convert_rc) {
		DE("The string is not a valid IP address: |%s|\n", ip->ip);
		return -3;
	}

	DD("2\n");

	if (convert_rc < 0) {
		DE("Could not convert string addredd |%s| to binary\n", ip->ip);
		return -4;
	}
	DD("3\n");

	s_addr.sin_port = htons(ip->port);

	DD("4\n");

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		DE("could not create socket\n");
		return (-1);
	}

	DD("5\n");

	if (connect(sock, (struct sockaddr *)&s_addr, (socklen_t)sizeof(s_addr)) < 0) {
		DE("could not connect to server\n");
		close(sock);
		return (-2);
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
int pepa_copy_fd_to_fd(int fd_from, int fd_to)
{
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


/* File descriptor of IN file, i.e., a file to read from */
int fd_out  = -1;
/* File descriptor of OUT file, i.e., a file write to */
int fd_in   = -1;

/* File descriptor on an opened socket */
int fd_sock = -1;

char *file_name = NULL;

/**
 * @author Sebastian Mountaniol (7/18/23)
 * @brief This function is a loop, where all file descriptors
 *  	  are listened, and messages moved between them.
 * @param ip_port_t* ip      
 * @param FILE* file_in 
 * @param FILE* file_out
 * @details 
 */
void pepa_merry_go_round(const int sckt, int _fd_in, const int _fd_out)
{
	fd_set         rfds;

	/* Select related variables */
	struct timeval tv;
	int            retval          = -1;

	uint64_t       accum_from_sock = 0;
	uint64_t       accum_to_sock   = 0;

	const int      max_fd          = MAX(sckt, _fd_in);

	while (1) {

		if (-1 == fcntl(_fd_in, F_GETFL)) {
			fd_in = pepa_open_pipe_in(file_name);
			if (fd_in < 0) {
				DE("Can not reopen IN file\n");
				perror("Can not reopen IN file: ");
				abort();
			}
			_fd_in = fd_in;
		}

		/* Set the FIFO signal fd into select set */
		FD_ZERO(&rfds);
		FD_SET(max_fd, &rfds);

		/* Wait 5 seconds */
		tv.tv_sec = 5;
		tv.tv_usec = 0;

		/* Wait a signal from the second side of the FIFO */

		DD("Going to wait on select() for 5 seconds\n");

		retval = select(max_fd + 1, &rfds, NULL, NULL, &tv);

		/* nothing to read */
		if (retval == 0) {
			continue;
		}

		/* The only case when select() returns -1 and it is legal, it is a signal;
		 * we ignore signals and continue select() */
		if ((retval == -1) && (errno == EINTR)) {
			continue;
		}

		/* Any other case the select() returns -1, we have to stop */
		if (retval == -1) {
			perror("select()");
			abort();
		}

		/* Is there anything on socket? */
		if (FD_ISSET(sckt, &rfds)) {
			DD("Received something on socket\n");

			/* Read from socket, write to fd_write */
			const int32_t rc_copy = pepa_copy_fd_to_fd(sckt, _fd_out);
			if (rc_copy < 0) {
				/* something is wrong is there */
				abort();
			}

			DD("Copied from socket to FD OUT: %d bytes\n", rc_copy);

			accum_from_sock += rc_copy;
		}

		/* Is there anything on fd_read ? */
		if (FD_ISSET(_fd_in, &rfds)) {

			DD("Received something on FD IN\n");
			/* Read from IN pipe, write to socket */
			const int32_t rc_copy = pepa_copy_fd_to_fd(_fd_in, sckt);
			if (rc_copy < 0) {
				/* something is wrong is there */
				abort();
			}

			DD("Copied from FD OUT to socket: %d bytes\n", rc_copy);

			accum_to_sock += rc_copy;
		}

		DD("SOCK BYTES: FROM SOCK: %lu, TO SOCK: %lu\n", accum_from_sock, accum_to_sock);

		/* We are done, continue */
	}
}




void bye(void)
{
	if (fd_out >= 0) {
		close(fd_out);
	}

	if (fd_in >= 0) {
		close(fd_in);
	}
	if (fd_sock >= 0) {
		close(fd_sock);
	}
}

int main(int argi, char *argv[])
{
	/* IP address to connect to a server */
	ip_port_t *ip     = NULL;

	atexit(bye);

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
		{"addr",             required_argument,      0, 'a'},
		{"out",              required_argument,       0, 'o'},
		{"in",               required_argument,      0, 'i'},
		{0, 0, 0, 0}
	};


	int                  opt;
	int                  option_index   = 0;
	while ((opt = getopt_long(argi, argv, ":a:o:i:h", long_options, &option_index)) != -1) {
		switch (opt) {
		case 'a': /* Address to connect to */
			ip = pepa_parse_ip_string(optarg);
			TESTP_ASSERT(ip);
			DD("Addr OK: |%s| : |%d|\n", ip->ip, ip->port);

			break;
		case 'o': /* Output file - write received from socket */
			fd_out = pepa_open_file_out(optarg);
			if (fd_out < 0) {
				DE("Can not open OUT file\n");
				abort();
			}
			DD("Got FD OUT - ok\n");
			break;
		case 'i': /* Input file - read and send to socket */
			fd_in = pepa_open_pipe_in(optarg);
			file_name = strdup(optarg);
			if (fd_in < 0) {
				DE("Can not open IN file\n");
				abort();
			}
			DD("Got FD IN - ok\n");
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

	/* Test that all needed arguments are accepted.
	   We need IP + PORT, file_in and file_out */

	TESTP_ASSERT_MES(ip, "No IP + PORT");

	fd_sock = pepa_connect_to_server(ip);

	if (fd_sock < 0) {
		DE("Can connect to server : |%s| |%d|\n", ip->ip, ip->port);
		pepa_ip_port_t_release(ip);
		abort();
	}

	DD("Connected to the server - OK\n");


	if (fd_out < 0) {
		DE("Can not open OUT file\n");
		abort();
	}

	if (fd_in < 0) {
		DE("Can not open OUT file\n");
		abort();
	}

	DD("Ready to go:\n"
	   "IP is: %s, PORT is %d\n"
	   "FD SOCK: %d, FD IN: %d, FD OUT: %d\n",
	   ip->ip, ip->port, fd_sock, fd_in, fd_out);

	pepa_ip_port_t_release(ip);

	pepa_merry_go_round(fd_sock, fd_in, fd_out);
	return 0;
}

