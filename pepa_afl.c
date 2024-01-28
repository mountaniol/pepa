/**
 * This file is implementation of AFL+ fussier tester API.
 * We don't care about function calls validity here;
 * We just run functions with random params, and want to acheive
 * crach situation to debug it.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <limits.h>

#include "slog/src/slog.h"
#include "pepa_config.h"
#include "pepa_socket_common.h"
#include "pepa_errors.h"
#include "pepa_core.h"
#include "pepa_state_machine.h"
#include "pepa3.h"
#include "pepa_in_reading_sockets.h"

__AFL_FUZZ_INIT(void);

#define MAX_ITERATIONS (1000000)

#define INPUTSIZE (sizeof(pepa_core_t) * 2)
#define MIN_INPUTSIZE (sizeof(pepa_core_t) * 2)

pepa_core_t *afl_fill_core_with_input(char *input, size_t input_size);

pepa_core_t *afl_fill_core_with_input(char *input,
									  __attribute__((unused)) size_t input_size)
{
	pepa_core_init();
	pepa_core_t *core = pepa_get_core();
	memcpy(core, input, sizeof(pepa_core_t));
	return core;
}

/* This function tests that pepa_go function doesnt' accept invalid core structure */
int test_0(char *input,
		   __attribute__((unused)) size_t input_size)
{
	pepa_core_t *core = afl_fill_core_with_input(input, input_size);
	TESTP(core, 1);
	pepa_go(core);

	pepa_core_release(core);
	return 0;
}

/* This function tests that pepa3_close_sockets() works even with invalid core strcture */
int test_1(char *input,
		   __attribute__((unused)) size_t input_size)
{
	pepa_core_t *core = afl_fill_core_with_input(input, input_size);
	TESTP(core, 1);
	pepa3_close_sockets(core);
	pepa_core_release(core);
	return 0;
}

/* This function tests that pepa_in_reading_sockets_allocate() handles extreme situations */
int test_2(char *input,
		   __attribute__((unused)) size_t input_size)
{
	int number_of_iterations = *((int *)input);
	pepa_core_init();
	pepa_core_t *core = pepa_get_core();

	if (number_of_iterations > MAX_ITERATIONS) {
		number_of_iterations = MAX_ITERATIONS;
	}

	for (int i = 0; i < number_of_iterations; i++) {
		pepa_in_reading_sockets_allocate(core, i);
		pepa_in_reading_sockets_free(core);
	}
	return 0;
}

/* This test checks that pepa_in_reading_sockets_add()
 * and pepa_in_reading_sockets_close_rm()
 * can handle extreme situations */
int test_3(char *input,
		   __attribute__((unused)) size_t input_size)
{
	int number_of_iterations = *((int *)input);
	pepa_core_init();
	pepa_core_t *core = pepa_get_core();

	if (number_of_iterations > MAX_ITERATIONS) {
		number_of_iterations = MAX_ITERATIONS;
	}

	pepa_in_reading_sockets_allocate(core, 256);

	for (int i = 0; i < number_of_iterations; i++) {
		pepa_in_reading_sockets_add(core, i);
	}

	for (int i = 0; i < number_of_iterations; i++) {
		pepa_in_reading_sockets_close_rm(core, i);
	}

	pepa_in_reading_sockets_free(core);
	pepa_core_release(core);
	return 0;
}

/* This test checks that pepa_in_reading_sockets_add()
 * and pepa_in_reading_sockets_free()
 * can handle extreme situations */
int test_4(char *input,
		   __attribute__((unused)) size_t input_size)
{
	int number_of_iterations = *((int *)input);
	pepa_core_init();
	pepa_core_t *core = pepa_get_core();

	pepa_in_reading_sockets_allocate(core, 256);

	if (number_of_iterations > MAX_ITERATIONS) {
		number_of_iterations = MAX_ITERATIONS;
	}

	for (int i = 0; i < number_of_iterations; i++) {
		pepa_in_reading_sockets_add(core, i);
	}

	pepa_in_reading_sockets_free(core);
	pepa_core_release(core);
	return 0;
}

/* This function tests that pepa_socket_close_in_listen() function doesnt' accept invalid core structure */
int test_5(char *input,
		   __attribute__((unused)) size_t input_size)
{
	pepa_core_t *core = afl_fill_core_with_input(input, input_size);
	TESTP(core, 1);
	pepa_socket_close_in_listen(core);
	pepa_core_release(core);
	return 0;
}

int test_6(char *input,
		   __attribute__((unused)) size_t input_size)
{
	int sock_num = *((int *)input);
	pepa_reading_socket_close(sock_num, NULL);
	pepa_reading_socket_close(sock_num, "TEST");
	return 0;
}

int test_7(char *input,
		   __attribute__((unused)) size_t input_size)
{
	int sock_num = *((int *)input);
	pepa_socket_close(sock_num, NULL);
	pepa_socket_close(sock_num, "TEST");
	return 0;
}

int test_8(char *input,
		   __attribute__((unused)) size_t input_size)
{
	int sock_num = *((int *)input);
	pepa_socket_shutdown_and_close(sock_num, NULL);
	pepa_socket_shutdown_and_close(sock_num, "TEST");
	return 0;
}

int test_9(char *input,
		   __attribute__((unused)) size_t input_size)
{
	int            *input_int = (int *)input;
	const int      epfd       = *input;
	const int      fd         = *(input + 1);
	const uint32_t events     = (uint32_t)*(input + 2);

	epoll_ctl_add(epfd, fd, events);
	return 0;
}

int test_10(char *input,
			__attribute__((unused)) size_t input_size)
{
	int            *input_int = (int *)input;
	pepa_test_fd(*input_int);
	return 0;
}

#if 0 /* SEB */
/* This function tests that pepa3_close_sockets() works even with invalid core strcture */
int test_11(char *input,
			__attribute__((unused)) size_t input_size){
	pepa_core_t *core = afl_fill_core_with_input(input, input_size);
	TESTP(core, 1);
	pepa_thread_kill_monitor(core);
	return 0;
}
#endif

#if 0 /* SEB */
int test_12(char *input, __attribute__((unused))
			size_t input_size){
	return 0;
}
#endif

#define NUMBER_OF_TESTS (12)

//#ifdef __AFL_HAVE_MANUAL_CONTROL
//__AFL_INIT(void);
//#endif


int fuzzer(void)
{
	int           rc;
	// We use here build-int card type file */
	int           len;
	char          input[INPUTSIZE] = {0};
	//ssize_t       buf_len;
	//unsigned char buf[1024000];
#define __AFL_FUZZ_TESTCASE_LEN buf_len
#define __AFL_FUZZ_TESTCASE_BUF buf

	__AFL_INIT();

	unsigned char  *buf              = __AFL_FUZZ_TESTCASE_BUF;

	len = read(STDIN_FILENO, input, INPUTSIZE);

	if (len < MIN_INPUTSIZE) {
		return 1;
	}

	while (__AFL_LOOP(UINT_MAX)) {
		switch (input[0] % NUMBER_OF_TESTS) {
		case 0:
			rc = test_0(buf, len);
			if (rc) {
				return rc;
			}

			break;
			// return 0;
		case 1:
			rc = test_1(buf, len);
			if (rc) {
				return rc;
			}
			break;
			// return 0;
		case 2:
			rc = test_2(buf, len);
			if (rc) {
				return rc;
			}

			break;
			// return 0;
		case 3:
			rc = test_3(buf, len);
			if (rc) {
				return rc;
			}

			break;
			//return 0;
		case 4:
			rc = test_4(buf, len);
			if (rc) {
				return rc;
			}

			break;
		case 5:
			rc = test_5(buf, len);
			if (rc) {
				return rc;
			}

			break;
		case 6:
			rc = test_6(buf, len);
			if (rc) {
				return rc;
			}

			break;
		case 7:
			rc = test_7(buf, len);
			if (rc) {
				return rc;
			}

			break;
		case 8:
			rc = test_8(buf, len);
			if (rc) {
				return rc;
			}

			break;
		case 9:
			rc = test_9(buf, len);
			if (rc) {
				return rc;
			}

			break;
		case 10:
			rc = test_10(buf, len);
			if (rc) {
				return rc;
			}

			break;
			//case 11:
			//return test_11(input, len);
			//case 12:
			//return test_12(input, len);

			//return 0;
		default:
			break;
		}
	}
	return 0;
}

int main(void)
{
	//int ret = 0;
	//while (__AFL_LOOP(10000)) ret |= fuzzer();
	//while (__AFL_LOOP(10000)) ret |= fuzzer();
	//return ret;
	return fuzzer();
}
