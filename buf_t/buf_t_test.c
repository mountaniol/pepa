#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "buf_t.h"
// #include "buf_t_string.h"
// #include "buf_t_array.h"
#include "buf_t_stats.h"
#include "buf_t_debug.h"

int verbose = 1;

#define PRINT(fmt, ...) do{if(verbose > 0){printf("%s +%d : ", __func__, __LINE__); printf(fmt, ##__VA_ARGS__);} }while(0 == 1)
#define PSPLITTER()  do{if(verbose > 0)printf("+++++++++++++++++++++++++++++++++++++++++++++++\n\n");} while(0)
#define PSPLITTER2()  do{if(verbose > 0) printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");} while(0)
#define PSTART(x, num)     do{if(verbose > 0) printf("Beginning:   [%s] / test # %.3d\n", x, num);} while(0)
#define PSTEP(x)      do{if(verbose > 0) printf("Passed step: [%s] +%.3d\n", x, __LINE__);} while(0)
#define PSUCCESS(x, num) do{PSPLITTER2();printf("PASS:        [%s] / test # %.3d\n", x, num);} while(0)
#define PFAIL(x, num)   do{PSPLITTER2(); printf("FAIL:        [%s] / test # %.3d [line +%d]\n", x, num, __LINE__);} while(0)

/**
 * @author Sebastian Mountaniol (11/22/23)
 * @brief Allocate buf_t with given size; abort on falure
 * @param const int test_num Test number
 * @param const char* from_func Caller function
 * @param const int line     Line from where it was called
 * @return buf_t* Allocated buffer 
 * @details 
 */
buf_t *allocate_buf(const buf_s64_t sz, const int test_num, const char *from_func, const int line)
{
	buf_t *buf = buf_new(sz);
	if (NULL == buf) {
		DE("Cant allocate buf size: %ld / from %s +%d", sz, from_func, line);
		PFAIL("Cant allocate buf", test_num);
		abort();
	}

	return buf;
}

/**
 * @author Sebastian Mountaniol (11/22/23)
 * @brief Free the buffer; abort on error on 
 * @param buf_t* buf      Buffer to free
 * @param const int test_num Test number
 * @param const char* from_func Caller function
 * @param const int line     Line from where it was called
 * @details 
 */
void free_buf(buf_t *buf, const int test_num, const char *from_func, const int line)
{
	if (BUFT_OK != buf_free(buf)) {
		DE("Cant free buf / from %s +%d", from_func, line);
		PFAIL("Can not free the buffer", test_num);
		abort();
	}
}


/**** Function tests ****/
#if 0 /* SEB */

void test_func_(int test_num){}

void test_func_buf_get_data_ptr(int test_num){}

void test_func_buf_data_is_null(int test_num){}

void test_func_buf_is_valid(int test_num){}

void test_func_buf_to_data(int test_num){}

void test_func_buf_clean(int test_num){}
void test_func_buf_add_room(int test_num){}
void test_func_buf_test_room(int test_num){}

void test_func_buf_set_used(int test_num){}

void test_func_buf_inc_used(int test_num){}

void test_func_buf_dec_used(int test_num){}

void test_func_buf_room(int test_num){}

void test_func_buf_set_room(int test_num){}

void test_func_buf_inc_room(int test_num){}

void test_func_buf_dec_room(int test_num){}

void test_func_buf_pack(int test_num){}

void test_func_buf_is_change_allowed(int test_num){}

void test_func_buf_lock(int test_num){}

void test_func_buf_set_canary(int test_num){}

void test_func_buf_force_canary(int test_num){}

void test_func_buf_test_canary(int test_num){}

void test_func_buf_get_canary(int test_num){}

void test_func_buf_detect_used(int test_num){}
#endif

#if 0 /* SEB */
void test_func_(int test_num){}
#endif





/**** Functional tests ****/

/* Create buffer with 0 size data */
void test_buf_new_zero_size(int test_num)
{
	buf_t *buf = NULL;
	PSPLITTER();

	PSTART("allocate 0 size buffer", test_num);
	buf = allocate_buf(0, test_num, __func__, __LINE__);

	PSTEP("Buffer allocated with 0 room size");

	if (buf_get_used_count(buf) != 0 || buf_get_room_count(buf) != 0) {
		printf("0 size buffer: used (%ld) or room (%ld) != 0\n", buf_get_used_count(buf), buf_get_room_count(buf));
		PFAIL("0 size buffer", test_num);
		abort();
	}

	PSTEP("Tested: buf->used == 0 and buf->room == 0");

	//if (buf->data != NULL) {
	if (buf_data_is_null(buf) == BUFT_NO) {
		printf("0 size buffer: data != NULL (%p)\n", buf_get_data_ptr(buf));
		PFAIL("0 size buffer", test_num);
		abort();
	}

	PSTEP("Tested: buf->data == NULL");

	free_buf(buf, test_num, __func__, __LINE__);
	PSTEP("Released buf");
	PSUCCESS("allocate 0 size buffer", test_num);
}

/* Create buffers with increasing size */
void test_buf_new_increasing_size(int test_num)
{
	buf_t    *buf = NULL;
	uint64_t size = 64;
	int      i;
	PSPLITTER();

	PSTART("increasing buffer internal size", test_num);
	for (i = 1; i < 16; i++) {
		size = size << 1;

		buf = allocate_buf(size, test_num, __func__, __LINE__);

		if ((uint64_t)buf_get_used_count(buf) != 0 || (uint64_t)buf_get_room_count(buf) != size) {
			/*@ignore@*/
			printf("increasing size buffer: used (%ld) !=0 or room (%ld) != %zu\n", buf_get_used_count(buf), buf_get_room_count(buf), size);
			/*@end@*/
			PFAIL("increasing size buffer", test_num);
			abort();
		}

		//if (NULL == buf->data) {
		if (buf_data_is_null(buf) == BUFT_YES) {
			/*@ignore@*/
			printf("increasing size buffer: data == NULL (%p), asked size: %zu, iteration: %d\n", buf_get_data_ptr(buf), size, i);
			/*@end@*/
			PFAIL("increasing size buffer", test_num);
			abort();
		}

		free_buf(buf, test_num, __func__, __LINE__);

	}

	/*@ignore@*/
	PRINT("[Allocated up to %zu bytes buffer]\n", size);
	/*@end@*/

	PSUCCESS("increasing size buffer", test_num);
}

void test_buf_string(size_t buffer_init_size, int test_num)
{
	buf_t      *buf  = NULL;
	const char *str  = "Jabala Labala Hoom";
	const char *str2 = " Lalala";

	PSPLITTER();

	PSTART("buf_string", test_num);
	PRINT("Testing buffer init size: %zu\n", buffer_init_size);

	/*@ignore@*/
	PRINT("[Asked string size: %zu]\n", buffer_init_size);
	/*@end@*/

	buf = buf_string(buffer_init_size);
	if (NULL == buf) {
		PFAIL("buf_string: Can't allocate buf", test_num);
		abort();
	}

	PSTEP("Allocated buffer");

	if (BUFT_OK != buf_add(buf, str, strlen(str))) {
		PFAIL("buf_string: can't add", test_num);
		abort();
	}

	PSTEP("Adding str");

#if 0
	if (buf->used != (buf->room - 1)) {
		printf("[After buf_add: wrong buf->used or buf->room]\n");
		printf("[buf->used = %d, buf->room = %d]\n", buf->used, buf->room);
		printf("[bif->used should be = (buf->room - 1)]\n");
		PFAIL("buf_string failed", test_num);
		abort();
	}
#endif

	//if (strlen(buf->data) != strlen(str)) {
	if (strlen(buf_get_data_ptr(buf)) != strlen(str)) {
		/*@ignore@*/
		printf("[After buf_add: wrong string len of buf->data]\n");
		printf("[Added string len = %zu]\n", strlen(str));
		//printf("[buf->data len = %zu]\n", strlen(buf->data));
		printf("[buf->data len = %zu]\n", strlen(buf_get_data_ptr(buf)));
		/*@end@*/
		PFAIL("buf_string", test_num);
		abort();
	}

#if 0
	printf("After string: |%s|, str: |%s|\n", buf->data, str);
	printf("After first string: buf->room = %d, buf->used = %d\n", buf->room, buf->used);
#endif

	PSTEP("Tested str added");

	if (BUFT_OK != buf_add(buf, str2, strlen(str2))) {
		printf("[Can't add string into buf]\n");
		PFAIL("buf_string", test_num);
		abort();
	}

	if (buf_get_used_count(buf) != (buf_s64_t)strlen(str) + (buf_s64_t)strlen(str2)) {
		/*@ignore@*/
		printf("After buf_add: wrong buf->used\n");
		printf("Expected: buf->used = %zu\n", strlen(str) + strlen(str2));
		printf("Current : buf->used = %ld\n", buf_get_used_count(buf));
		printf("str = |%s| len = %zu\n", str, strlen(str));
		printf("str2 = |%s| len = %zu\n", str2, strlen(str2));
		/*@end@*/

		PFAIL("buf_string", test_num);
		abort();
	}

	//if (strlen(buf->data) != (strlen(str) + strlen(str2))) {
	if (strlen(buf_get_data_ptr(buf)) != (strlen(str) + strlen(str2))) {
		/*@ignore@*/
		printf("[buf->used != added strings]\n");
		//printf("[buf->used = %zu, added strings len = %zu]\n", strlen(buf->data), strlen(str) + strlen(str2));
		printf("[buf->used = %zu, added strings len = %zu]\n", strlen(buf_get_data_ptr(buf)), strlen(str) + strlen(str2));
		//printf("[String is: |%s|, added strings: |%s%s|]\n", buf->data, str, str2);
		printf("[String is: |%s|, added strings: |%s%s|]\n", (char *)buf_get_data_ptr(buf), str, str2);
		printf("str = |%s| len = %zu\n", str, strlen(str));
		printf("str2 = |%s| len = %zu\n", str2, strlen(str2));
		/*@end@*/
		PFAIL("buf_string", test_num);
		abort();
	}

	//printf("%s\n", buf->data);
	free_buf(buf, test_num, __func__, __LINE__);
	PSUCCESS("buf_string", test_num);
}

/* Allocate string buffer. Add several strings. Pack it. Test that after the packing the buf is
   correct. Test that the string in the buffer is correct. */
void test_buf_pack_string(int test_num)
{
	buf_t      *buf     = NULL;
	const char *str     = "Jabala Labala Hoom";
	const char *str2    = " Lalala";
	char       *con_str = NULL;
	buf_s64_t  len;
	buf_s64_t  len2;
	int        rc;

	PSPLITTER();

	PSTART("buf_pack_string", test_num);

	buf = buf_string(1024);
	if (NULL == buf) {
		PFAIL("buf_string: Can't allocate buf", test_num);
		abort();
	}

	PSTEP("Allocated buffer");

	len = strlen(str);

	if (BUFT_OK != buf_add(buf, str, len)) {
		PFAIL("buf_pack_string: can't add", test_num);
		abort();
	}

	PSTEP("Adding str");

	//if (strlen(buf->data) != strlen(str)) {
	if (strlen(buf_get_data_ptr(buf)) != strlen(str)) {
		PFAIL("buf_pack_string", test_num);
		abort();
	}

	PSTEP("Tested str added");


	len2 = strlen(str2);
	if (BUFT_OK != buf_add(buf, str2, len2)) {
		printf("[Can't add string into buf]\n");
		PFAIL("buf_pack_string", test_num);
		abort();
	}

	if (buf_get_used_count(buf) != (len + len2)) {
		PFAIL("buf_pack_string", test_num);
		abort();
	}

	//if ((buf_s64_t)strlen(buf->data) != (len + len2)) {
	if ((buf_s64_t)strlen(buf_get_data_ptr(buf)) != (len + len2)) {
		PFAIL("buf_pack_string", test_num);
		abort();
	}

	/* Now we pack the buf */
	if (BUFT_OK != buf_pack(buf)) {
		PFAIL("buf_pack_string", test_num);
		abort();
	}

	/* Test that the packed buffer has the right size */
	if (buf_get_used_count(buf) != (len + len2)) {
		DE("buf_used(buf) [%lu] != len + len2 [%lu]\n", buf_get_used_count(buf), (len + len2));
		PFAIL("buf_pack_string", test_num);
		abort();
	}

	/* Test that buf->room = buf->used + 1 */
	if (buf_get_used_count(buf) != buf_get_room_count(buf) - 1) {
		DE("buf used [%lu] != buf_room + 1 [%lu]\n", buf_get_used_count(buf), buf_get_room_count(buf));
		PFAIL("buf_pack_string", test_num);
		abort();
	}

	con_str = calloc(len + len2 + 1, 1);
	if (NULL == con_str) {
		printf("Error: can't allocate memory\n");
		abort();
	}

	rc = snprintf(con_str, len + len2 + 1, "%s%s", str, str2);
	if (rc != len + len2) {
		PFAIL("snprintf failed (this is very strange!)", test_num);
		abort();
	}

	//if (0 != strcmp(buf->data, con_str)) {
	if (0 != strcmp(buf_get_data_ptr(buf), con_str)) {
		PFAIL("buf_pack_string: Strings are differ", test_num);
		abort();
	}
	//printf("%s\n", buf_data(buf));
	free_buf(buf, test_num, __func__, __LINE__);
	free(con_str);

	PSUCCESS("buf_pack_string", test_num);
}

/* Allocate string buffer. Add several strings. Pack it. Test that after the packing the buf is
   correct. Test that the string in the buffer is correct. */
void test_buf_str_concat(int test_num)
{
	buf_t      *buf1    = NULL;
	buf_t      *buf2    = NULL;
	const char *str1    = "Jabala Labala Hoom";
	const char *str2    = " Lalala";
	char       *con_str = NULL;
	buf_s64_t  len1;
	buf_s64_t  len2;

	int        rc;

	len1 = strlen(str1);
	len2 = strlen(str2);

	PSPLITTER();

	PSTART("buf_str_concat", test_num);

	buf1 = buf_string(0);
	if (NULL == buf1) {
		PFAIL("buf_str_concat: Can't allocate buf1", test_num);
		abort();
	}

	if (BUFT_OK != buf_add(buf1, str1, len1)) {
		PFAIL("buf_str_concat: Can't add string into buf2", test_num);
		abort();
	}

	if (BUFT_OK != buf_type_is_string(buf1)) {
		PFAIL("buf_str_concat: buf1 is not a string buffer", test_num);
		abort();
	}

	PSTEP("Allocated buf1");

	buf2 = buf_string(0);
	if (NULL == buf2) {
		PFAIL("buf_str_concat: Can't allocate buf2", test_num);
		abort();
	}

	if (BUFT_OK != buf_add(buf2, str2, len2)) {
		PFAIL("buf_str_concat: Can't add string into buf2", test_num);
		abort();
	}

	if (BUFT_OK != buf_type_is_string(buf2)) {
		PFAIL("buf_str_concat: buf2 is not a string buffer", test_num);
		abort();
	}

	PSTEP("Allocated buf2");

	if (BUFT_OK != buf_str_concat(buf1, buf2)) {
		PFAIL("buf_str_concat: buf_str_concat returned an error", test_num);
		abort();
	}

	PSTEP("buf_str_concat OK");

	//if ((buf_s64_t)strlen(buf1->data) != (len1 + len2)) {
	if ((buf_s64_t)strlen(buf_get_data_ptr(buf1)) != (len1 + len2)) {
		PFAIL("buf_str_concat: bad length", test_num);
		abort();
	}

	PSTEP("string length match 1");

	/* Test that the packed buffer has the right size */
	if (buf_get_used_count(buf1) != (len1 + len2)) {
		DE("buf_used(buf) [%lu] != len + len2 [%lu]\n", buf_get_used_count(buf1), (len1 + len2));
		PFAIL("buf_str_concat: wrong buf_used()", test_num);
		abort();
	}

	PSTEP("string length match 2");

	/* Test that buf->room = buf->used + 1 */
	if (buf_get_used_count(buf1) != buf_get_room_count(buf1) - 1) {
		DE("buf used [%lu] != buf_room + 1 [%lu]\n", buf_get_used_count(buf1), buf_get_room_count(buf1));
		PFAIL("buf_str_concat: buf_used(buf1) != buf_room(buf1) - 1", test_num);
		abort();
	}

	PSTEP("buf_used, buf_room OK");

	con_str = malloc(len1 + len2 + 1);
	if (NULL == con_str) {
		PFAIL("buf_str_concat: can't allocate memory\n", test_num);
		abort();
	}

	memset(con_str, 0, len1 + len2 + 1);
	rc = snprintf(con_str, len1 + len2 + 1, "%s%s", str1, str2);
	if (rc != len1 + len2) {
		PFAIL("snprintf failed (this is very strange!)", test_num);
		abort();
	}

	//if (0 != strcmp(buf1->data, con_str)) {
	if (0 != strcmp(buf_get_data_ptr(buf1), con_str)) {
		PFAIL("buf_str_concat: string is not the same", test_num);
		abort();
	}

	PSTEP("strings compared OK");

	free_buf(buf1, test_num, __func__, __LINE__);
	free_buf(buf2, test_num, __func__, __LINE__);
	free(con_str);

	PSUCCESS("buf_str_concat", test_num);
}

void test_buf_pack(int test_num)
{
	/*@only@*/ buf_t     *buf          = NULL;
	/*@only@*/ char      *_buf_data     = NULL;
	buf_s64_t buf_data_size = 256;
	buf_s64_t i;
	time_t    current_time  = time(0);
	srandom((unsigned int)current_time);

	PSPLITTER();

	PSTART("buf_pack", test_num);

	buf = allocate_buf(1024, test_num, __func__, __LINE__);

	PSTEP("Allocated buffer");

	_buf_data = calloc(256, 1);
	if (NULL == _buf_data) {
		PFAIL("Can't allocate a buffer", test_num);
		abort();
	}

	PSTEP("Allocated local buffer for random data");
	for (i = 0; i < buf_data_size; i++) {
		char randomNumber = (char)random();
		_buf_data[i] = randomNumber;
	}


	PSTEP("Filled local buffer with random data");

	/* Make sure that this buffer ended not with 0 */
	//buf_data[buf_data_size - 1] = 9;

	if (BUFT_OK != buf_add(buf, _buf_data, buf_data_size)) {
		PFAIL("buf_pack: can't add", test_num);
		abort();
	}

	PSTEP("Added buffer into buf_t");

	if (buf_get_used_count(buf) != buf_data_size) {
		PFAIL("buf_pack", test_num);
		abort();
	}

	/* Compare memory */
	//if (0 != memcmp(buf->data, buf_data, buf_data_size)) {

	if (0 != memcmp(buf_get_data_ptr(buf), _buf_data, buf_data_size)) {
		PFAIL("buf_pack", test_num);
		abort();
	}

	PSTEP("Compared memory");

	/* Now we pack the buf */
	if (BUFT_OK != buf_pack(buf)) {
		PFAIL("buf_pack", test_num);
		abort();
	}
	PSTEP("Packed buf_t");

	/* Test that the packed buffer has the right size */
	if (buf_get_used_count(buf) != buf_data_size) {
		PFAIL("buf_pack", test_num);
		abort();
	}
	PSTEP("That buf->used is right");

	/* Test that buf->room = buf->used + 1 */
	if (buf_get_used_count(buf) != buf_get_room_count(buf)) {
		printf("buf->room (%ld) != buf->used (%ld)\n", buf_get_room_count(buf), buf_get_used_count(buf));
		PFAIL("buf_pack", test_num);
		abort();
	}
	PSTEP("Tested room and used");

	//printf("%s\n", buf->data);
	free_buf(buf, test_num, __func__, __LINE__);
	free(_buf_data);

	PSUCCESS("buf_pack", test_num);
}

void test_buf_canary(int test_num)
{
	/*@notnull@*/ /*@only@*/  buf_t         *buf;
	/*@only@*/ char          *_buf_data;
	buf_s64_t     buf_data_size = 256;
	buf_s64_t     i;
	time_t        current_time  = time(0);
	buf_t_flags_t flags;

	srandom((unsigned int)current_time);

	PSPLITTER();

	PSTART("buf_canary", test_num);

	/* We need to save and later restore flags: during this test we must unset the 'abort' flags */
	flags = buf_save_flags();
	buf_unset_abort_flag();

	buf = allocate_buf(0, test_num, __func__, __LINE__);

	if (BUFT_OK != buf_mark_canary(buf)) {
		free_buf(buf, test_num, __func__, __LINE__);
		PFAIL("buf_canary: Can't set the CANARY flag", test_num);
		abort();
	}

	PSTEP("Allocated buffer");

	_buf_data = calloc(256, 1);
	if (NULL == _buf_data) {
		PFAIL("Can't allocate a buffer", test_num);
		free_buf(buf, test_num, __func__, __LINE__);
		abort();
	}

	PSTEP("Allocated local buffer for random data");

	for (i = 0; i < buf_data_size; i++) {
		char randomNumber = (char)random();
		_buf_data[i] = randomNumber;
	}

	PSTEP("Filled local buffer with random data");

	if (BUFT_OK != buf_add(buf, _buf_data, buf_data_size - 1)) {
		PFAIL("buf_pack: can't add", test_num);
		free_buf(buf, test_num, __func__, __LINE__);
		abort();
	}

	PSTEP("Added buffer into buf_t");

	if (buf_get_used_count(buf) != buf_data_size - 1) {
		PFAIL("buf_pack", test_num);
		if (BUFT_OK != buf_free(buf)) {
			PRINT("Can't release buffer");
			abort();
		}
		abort();
	}

	/* Compare memory */
	//if (0 != memcmp(buf->data, buf_data, buf_data_size - 1)) {
	if (0 != memcmp(buf_get_data_ptr(buf), _buf_data, buf_data_size - 1)) {
		PFAIL("buf_canary: buffer is wrong", test_num);
		free_buf(buf, test_num, __func__, __LINE__);
		abort();
	}

	PSTEP("Compared memory");

	/* Test canary */
	if (BUFT_OK != buf_test_canary(buf)) {
		PFAIL("buf_canary: bad canary", test_num);
		free_buf(buf, test_num, __func__, __LINE__);
		abort();
	}

	PSTEP("Canary word is OK for the buffer");

	/* Now we copy the full buffer into buf->data and such we break the canary pattern */
	//memcpy(buf->data, buf_data, buf_data_size);
	if (NULL != buf_get_data_ptr(buf)) {
		memcpy(buf_get_data_ptr(buf), _buf_data, buf_data_size);
	} else {
		PFAIL("buf_canary: buf_data(buf) is NULL - must be not", test_num);
		abort();
	}

	PSTEP("Corrupted buf: we expect an ERR");

	/* Test canary: we expect it to be wrong */
	if (BUFT_OK == buf_test_canary(buf)) {
		PFAIL("buf_canary: good canary but must be bad", test_num);
		free_buf(buf, test_num, __func__, __LINE__);
		abort();
	}

	printf("Ignore the previous ERR printout; we expected it\n");

	PSTEP("The canary is broken. It is what expected to be");

	if (BUFT_OK != buf_set_canary(buf)) {
		PFAIL("buf_canary: can't set canary on the buffer", test_num);
		free_buf(buf, test_num, __func__, __LINE__);
		abort();
	}

	PSTEP("Fixed canary");

	/* Test canary again */
	if (BUFT_OK != buf_test_canary(buf)) {
		PFAIL("buf_canary: bad canary but must be good", test_num);
		free_buf(buf, test_num, __func__, __LINE__);
		abort();
	}

	PSTEP("Now canary is OK");

	/* Run buf validation */
	if (BUFT_OK != buf_is_valid(buf)) {
		PFAIL("buf_canary: buffer is not valid", test_num);
		free_buf(buf, test_num, __func__, __LINE__);
		abort();
	}

	PSTEP("Buffer is valid");

	free_buf(buf, test_num, __func__, __LINE__);
	PSTEP("Buffer released");

	free(_buf_data);
	buf_restore_flags(flags);

	PSUCCESS("buf_canary", test_num);
}

/*** Test ARRAY buf ****/

void test_buf_array_zero_size(int test_num)
{
	buf_t *buf = NULL;
	PSPLITTER();

	PSTART("allocate 0 size array buffer", test_num);
	buf = buf_array(0, 0);
	if (NULL == buf) {
		PFAIL("Cant allocate 0 size buf array", test_num);
		abort();
	}

	PSTEP("Buffer array allocated with 0 room size");

	if (buf_get_used_count(buf) != 0 || buf_get_room_count(buf) != 0) {
		printf("0 size buffer: used (%ld) or room (%ld) != 0\n", buf_get_used_count(buf), buf_get_room_count(buf));
		PFAIL("0 size buffer", test_num);
		abort();
	}

	PSTEP("Tested: buf->used == 0 and buf->room == 0");

	//if (buf->data != NULL) {
	if (buf_data_is_null(buf) == BUFT_NO) {
		printf("0 size buffer: data != NULL (%p)\n", buf_get_data_ptr(buf));
		PFAIL("0 size buffer", test_num);
		abort();
	}

	PSTEP("Tested: buf->data == NULL");

	free_buf(buf, test_num, __func__, __LINE__);
	PSTEP("Released buf");
	PSUCCESS("allocate 0 size buffer", test_num);
}

void test_buf_array_allocate_with_size(int test_num, int element_size, int num_of_elements)
{
	buf_t *buf = NULL;
	PSPLITTER();

	PSTART("allocate array buffer with size", test_num);
	buf = buf_array(element_size, num_of_elements);
	if (NULL == buf) {
		PFAIL("Cant allocate buf array", test_num);
		abort();
	}

	PSTEP("Buffer array allocated with 0 room size");

	int memory_to_expect = element_size * num_of_elements;

	if (buf_get_used_count(buf) != 0) {
		printf("Arr buffer: used expected 0, but it is (%ld)\n",
			   buf_get_used_count(buf));
		PFAIL("Arr buffer, used is wrong", test_num);
		abort();
	}

	if (buf_get_room_count(buf) != memory_to_expect) {
		printf("Arr buffer: oom (%ld), expected (%d)\n",
			   buf_get_room_count(buf), memory_to_expect);
		PFAIL("Arr size buffer, room is worng", test_num);
		abort();
	}

	if (buf_arr_get_members_count(buf) != 0) {
		printf("Arr size buffer: buf->arr.members (%d) != 0 as expected\n",
			   buf_arr_get_members_count(buf));
		PFAIL("Array buffer: buf->arr.members != 0", test_num);
		abort();
	}

	if (buf_arr_get_member_size(buf) != element_size) {
		printf("Arr size buffer: buf->arr.size != element_size (%d) != (%d)\n",
			   buf_arr_get_member_size(buf), element_size);
		PFAIL("Array buffer: buf->arr.size", test_num);
		abort();
	}

	PSTEP("Tested: buf->arr.size, buf->arr.members");

	//if (buf->data != NULL) {
	if (buf_data_is_null(buf) == BUFT_YES) {
		printf("Array size buffer: data == NULL (%p)\n", buf_get_data_ptr(buf));
		PFAIL("Array buffer, buf_data_is_null(buf) == YES", test_num);
		abort();
	}

	PSTEP("Tested: buf->data != NULL");

	free_buf(buf, test_num, __func__, __LINE__);
	PSTEP("Released buf");
	PSUCCESS("Allocate array buffer", test_num);
}

void test_buf_array_allocate_add(int test_num, int num_of_elements)
{
	buf_t *buf = NULL;
	PSPLITTER();

	PSTART("Add members to the array buffer", test_num);
	buf = buf_array(sizeof(int), num_of_elements);
	if (NULL == buf) {
		PFAIL("Cant allocate buf array", test_num);
		abort();
	}

	PSTEP("Buffer array allocated");

	/* Add element to buffer up to num_of_elements*/
	int counter;

	for (counter = 0; counter < num_of_elements; counter++) {
		ret_t rc = buf_arr_add(buf, &counter);
		if (BUFT_OK != rc) {
			printf("Failed to add an element, counter = %d\n", counter);
			PFAIL("Array buffer, adding member", test_num);
		}
	}

	PSTEP("Added memebers");

	if (buf_data_is_null(buf) == BUFT_YES) {
		printf("Array size buffer: data == NULL (%p)\n", buf_get_data_ptr(buf));
		PFAIL("Array buffer, buf_data_is_null(buf) == YES", test_num);
		abort();
	}

	PSTEP("buf->data != NULL");

	if (buf_arr_get_members_count(buf) != num_of_elements) {
		printf("Array size buffer: number of members is (%d), expected (%d)\n",
			   buf_arr_get_members_count(buf), num_of_elements);
		PFAIL("Array buffer, wrong number of elements", test_num);
		abort();
	}

	PSTEP("Tested: buf_arr_members(buf) == num_of_elements");

	free_buf(buf, test_num, __func__, __LINE__);
	PSTEP("Released buf");
	PSUCCESS("Add members to the array buffer", test_num);
}

void test_buf_array_allocate_add_remove(int test_num, int num_of_elements)
{
	buf_t *buf = NULL;
	PSPLITTER();

	PSTART("Add and remove members to the array buffer", test_num);
	buf = buf_array(sizeof(int), num_of_elements);
	if (NULL == buf) {
		PFAIL("Cant allocate buf array", test_num);
		abort();
	}

	PSTEP("Buffer array allocated");

	/* Add element to buffer up to num_of_elements*/
	int counter;

	for (counter = 0; counter < num_of_elements; counter++) {
		ret_t rc = buf_arr_add(buf, &counter);
		if (BUFT_OK != rc) {
			printf("Failed to add an element, counter = %d\n", counter);
			PFAIL("Array buffer, adding member", test_num);
		}
	}

	PSTEP("Added memebers");

	if (buf_data_is_null(buf) == BUFT_YES) {
		printf("Array size buffer: data == NULL (%p)\n", buf_get_data_ptr(buf));
		PFAIL("Array buffer, buf_data_is_null(buf) == YES", test_num);
		abort();
	}

	PSTEP("buf->data != NULL");

	if (buf_arr_get_members_count(buf) != num_of_elements) {
		printf("Array size buffer: number of members is (%d), expected (%d)\n",
			   buf_arr_get_members_count(buf), num_of_elements);
		PFAIL("Array buffer, wrong number of elements", test_num);
		abort();
	}

	PSTEP("Tested: buf_arr_members(buf) == num_of_elements");

	/* Remove element by element */

	for (counter = 0; counter < num_of_elements; counter++) {
		ret_t rc = buf_arr_rm(buf, 0);
		if (BUFT_OK != rc) {
			printf("Failed to remove 0 element, counter = %d\n", counter);
			PFAIL("Array buffer, removing member", test_num);
		}

		/* Number of elements must be decreased by counter */
		if (buf_arr_get_members_count(buf) != num_of_elements - counter - 1) {
			printf("After removing %d element count of elements expected %d but it is %d\n",
				   counter + 1, num_of_elements - counter - 1, buf_arr_get_members_count(buf));
			PFAIL("Array buffer, removing member", test_num);
		}
	}

	PSTEP("Tested: removed all members");

	free_buf(buf, test_num, __func__, __LINE__);
	PSTEP("Released buf");
	PSUCCESS("Add and remove members to the array buffer", test_num);
}


void test_buf_array_merger(int test_num, int num_of_elements)
{
	buf_t *buf_src = NULL;
	buf_t *buf_dst = NULL;
	int rc;

	PSPLITTER();

	PSTART("Merge array buffers", test_num);
	buf_src = buf_array(sizeof(int), num_of_elements);
	if (NULL == buf_src) {
		PFAIL("Cant allocate buf array", test_num);
		abort();
	}

	buf_dst = buf_array(sizeof(int), num_of_elements);
	if (NULL == buf_dst) {
		PFAIL("Cant allocate buf array", test_num);
		abort();
	}

	PSTEP("Buffer array allocated");

	/* Add element to buffer up to num_of_elements*/
	int counter;

	for (counter = 0; counter < num_of_elements; counter++) {
		ret_t rc = buf_arr_add(buf_dst, &counter);
		if (BUFT_OK != rc) {
			printf("Failed to add an element, counter = %d\n", counter);
			PFAIL("Array buffer, adding member", test_num);
		}
	}

	for (counter = num_of_elements; counter < num_of_elements * 2; counter++) {
		ret_t rc = buf_arr_add(buf_src, &counter);
		if (BUFT_OK != rc) {
			printf("Failed to add an element, counter = %d\n", counter);
			PFAIL("Array buffer, adding member", test_num);
		}
	}

	PSTEP("Added memebers to bot buffers");

	if (buf_data_is_null(buf_src) == BUFT_YES) {
		printf("Array size src buffer: data == NULL (%p)\n", buf_get_data_ptr(buf_src));
		PFAIL("Array src buffer, buf_data_is_null(buf) == YES", test_num);
		abort();
	}

	if (buf_data_is_null(buf_dst) == BUFT_YES) {
		printf("Array size dst buffer: data == NULL (%p)\n", buf_get_data_ptr(buf_dst));
		PFAIL("Array dst  buffer, buf_data_is_null(buf) == YES", test_num);
		abort();
	}


	PSTEP("buf->data != NULL");

	if (buf_arr_get_members_count(buf_src) != num_of_elements) {
		printf("Array size src buffer: number of members is (%d), expected (%d)\n",
			   buf_arr_get_members_count(buf_src), num_of_elements);
		PFAIL("Array src buffer, wrong number of elements", test_num);
		abort();
	}

	if (buf_arr_get_members_count(buf_dst) != num_of_elements) {
		printf("Array size dst buffer: number of members is (%d), expected (%d)\n",
			   buf_arr_get_members_count(buf_dst), num_of_elements);
		PFAIL("Array dst buffer, wrong number of elements", test_num);
		abort();
	}

	PSTEP("Tested: buf_arr_members(buf_dst) == num_of_elements");

	/* Remove element by element */

	rc = buf_arr_merge(buf_dst, buf_src);
	if (BUFT_OK != rc) {
		printf("Failed buf_arr_merge, returned error: %s\n", buf_error_code_to_string(rc));
		PFAIL("Merge function failed", test_num);
		abort();
	}

	PSTEP("Tested: Merged buffers");

	/* Test buffers content */

	if (0 != buf_arr_get_members_count(buf_src)) {
		printf("Buf src is > 0 (%d), expected 0: ", buf_arr_get_members_count(buf_src));
		PFAIL("Src buffer is not empty", test_num);
		abort();
	}

	if ((num_of_elements * 2) != buf_arr_get_members_count(buf_dst)) {
		DE("Buf dst wrong count: expected %d, but it is %d \n",
		   (counter * 2), buf_arr_get_members_count(buf_src));
		PFAIL("Dst buffer wrong count", test_num);
		abort();
	}


	/* Text elements */
	for (counter = 0; counter < num_of_elements * 2; counter++) {
		int *tmp = buf_arr_get_member_ptr(buf_dst, counter);
		if (*tmp != counter) {
			printf("Element %d is wrong: expected value %d, but it is %d\n",
				   counter, counter, *tmp);
			PFAIL("Elements check failed", test_num);
		}
	}

	free_buf(buf_src, test_num, __func__, __LINE__);
	free_buf(buf_dst, test_num, __func__, __LINE__);
	PSTEP("Released buf");
	PSUCCESS("Add and remove members to the array buffer", test_num);
}

int main(void)
{
	/* Abort on any error */
	D("This is an regular D print\n");
	DD("This is an extended DD print\n");
	DDD("This is an extra extended DDD print\n");
	DE("This is an error DE print\n");
	DDE("This is an extended DDE error print\n");
	DDDE("This is an extra extended DDDE error print\n");

	buf_set_abort_flag();
	test_buf_new_zero_size(1);
	test_buf_new_increasing_size(1);
	test_buf_string(0, 1);
	test_buf_string(1, 2);
	test_buf_string(32, 3);
	test_buf_string(1024, 4);
	test_buf_pack_string(1);
	test_buf_str_concat(1);
	test_buf_pack(1);
	test_buf_canary(1);
	test_buf_array_zero_size(1);
	test_buf_array_allocate_with_size(1, 1, 4);
	test_buf_array_allocate_with_size(2, 100, 4);
	test_buf_array_allocate_with_size(3, 1, 8);
	test_buf_array_allocate_with_size(4, 100, 8);

	test_buf_array_allocate_add(1, 1);
	test_buf_array_allocate_add(2, 10);
	test_buf_array_allocate_add(3, 1000);

	test_buf_array_allocate_add_remove(1, 1);
	test_buf_array_allocate_add_remove(2, 10);
	test_buf_array_allocate_add_remove(3, 1000);

	test_buf_array_merger(1, 1);
	test_buf_array_merger(1, 10);
	test_buf_array_merger(1, 1000);

	PSUCCESS("All tests passed, good work!", 0);
	buf_t_stats_print();
	return (0);
}
