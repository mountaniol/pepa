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
#include "debug.h"

/* Max length of string IP address */
#define IP_LEN   (24)

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
 * @return FILE File descriptor on success, NULL on an error
 * @details THe file must exists
 */
static FILE *pepa_open_pipe_in(char *file_name)
{
	TESTP_ASSERT(file_name);
	FILE *fd = fopen(file_name, "r");
	if (NULL == fd) {
		DE(">>> Can not open file: %s", strerror(errno));
	}
	TESTP_ASSERT_MES(fd, "Can not open IN file");
	return fd;
}

/**
 * @author Sebastian Mountaniol (7/17/23)
 * @brief Open a file for OUT, i.e. to write from socket 
 * @param char _file_name File name to open. 
 * @return FILE File descriptor or NULL on an error
 * @details FIle MUST exist, not created! Also, the content of
 *  		the file will not be trunkated, i.e. opened in "a"
 *  		mode. It is up to end user to decide either the file
 *  		should be cleaned before a writing will start
 */
static FILE *pepa_open_file_out(char *file_name)
{
	TESTP_ASSERT(file_name);
	FILE *fd = fopen(file_name, "a");
	if (NULL == fd) {
		DE(">>> Can not open file: %s", strerror(errno));
	}
	TESTP_ASSERT_MES(fd, "Can not open OUT file");
	return fd;
}

int main(int argi, char *argv[])
{
	/* IP address to connect to a server */
	ip_port_t *ip     = NULL;
	/* File descriptor of IN file, i.e., a file to read from */
	FILE      *fd_out = NULL;
	/* File descriptor of OUT file, i.e., a file write to */
	FILE      *fd_in  = NULL;

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
			TESTP_ASSERT_MES(fd_out, "Can not open OUT file");
			break;
		case 'i': /* Input file - read and send to socket */
			fd_in = pepa_open_pipe_in(optarg);
			TESTP_ASSERT_MES(fd_in, "Can not open IN file");
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
	TESTP_ASSERT_MES(fd_in, "No in file");
	TESTP_ASSERT_MES(fd_out, "No out file");

}

