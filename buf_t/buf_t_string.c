/** @file */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <netdb.h>
#include <errno.h>
#include <stddef.h>

#include "buf_t.h"
#include "buf_t_stats.h"
#include "buf_t_debug.h"
#include "buf_t_memory.h"

//#define BUF_DEBUG 1

/* TODO: Split this funtion into set of function per type */
/* Validate sanity of buf_t */
ret_t buf_str_is_valid(/*@in@*//*@temp@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	if (BUFT_OK != buf_type_is_string(buf)) {
		DE("Buffer is not string type\n");
		return (-BUFT_BAD_BUFT_TYPE);
	}

	/* If the buf is string the room must be greater than used */
	/* If there is a data, then:
	   the room must be greater than used, because we do not count terminating \0 */
	if ((BUFT_NO == buf_data_is_null(buf)) &&
		(buf_get_room_count(buf) <= buf_get_used_count(buf))) {
		DE("Invalid STRING buf: buf->used (%ld) >= buf->room (%ld)\n", buf_get_used_count(buf), buf_get_room_count(buf));
		TRY_ABORT();
		return (-BUFT_BAD_USED);
	}

	/* For string buffers only: check that the string is null terminated */
	/* If the 'used' area not '\0' terminated - invalid */
	if ((BUFT_NO == buf_data_is_null(buf)) &&
		('\0' != *((char *)buf_get_data_ptr(buf) + buf_get_used_count(buf)))) {
		DE("Invalid STRING buf: no '0' terminated\n");
		D0("used = %ld, room = %ld, last character = |%c|, string = %s\n", buf_get_used_count(buf), buf_get_room_count(buf), *(buf->data + buf_get_used_count(buf)), buf->data);
		DE("used = %ld, room = %ld, last character = |%c|, string = %s\n",
		   buf_get_used_count(buf), buf_get_room_count(buf), *((char *)buf_get_data_ptr(buf) + buf_get_used_count(buf)), (char *)buf_get_data_ptr(buf));
		TRY_ABORT();
		return (-ECANCELED);
	}

	DDD0("Buffer is valid\n");
	//buf_print_flags(buf);
	return (BUFT_OK);
}

/*@null@*/ buf_t *buf_string(buf_s64_t size)
{
	buf_t *buf = NULL;
	buf = buf_new(size);

	T_RET_ABORT(buf, NULL);

	if (BUFT_OK != buf_set_type(buf, BUF_TYPE_STRING)) {
		DE("Can't set STRING flag\n");
		abort();
	}

	int rc = buf_str_is_valid(buf);
	if (BUFT_OK != rc) {
		DE("Buf string is invalid: error %d: %s\n", rc, buf_error_code_to_string(rc));
		TRY_ABORT();
		rc = buf_free(buf);
		return (NULL);
	}
	return (buf);
}

/*@null@*/ buf_t *buf_from_string(/*@in@*//*@temp@*/char *str, const buf_s64_t size_without_0)
{
	/*@in@*/buf_t *buf = NULL;
	/* The string must be not NULL */
	T_RET_ABORT(str, NULL);

	/* Test that the string is '\0' terminated */
	if (*(str + size_without_0) != '\0') {
		DE("String is not null terminated\n");
		TRY_ABORT();
		return (NULL);
	}

	buf = buf_new(0);
	TESTP(buf, NULL);

	if (BUFT_OK != buf_set_type(buf, BUF_TYPE_STRING)) {
		DE("Can't set STRING flag\n");
		if (BUFT_OK != buf_free(buf)) {
			DE("Can't release a buffer\n");
		}
		TRY_ABORT();
		return (NULL);
	}
	/* We set string into the buf_t.
	 * The 'room' len contain null terminatior,
	 * the 'used' for string doesn't */
	if (BUFT_OK != buf_set_data(buf, str, size_without_0 + 1, size_without_0)) {
		DE("Can't set string into buffer\n");
		/* Just in case: Disconnect buffer from the buf_t before release it */
		if (BUFT_OK != buf_set_data(buf, NULL, 0, 0)) {
			DE("Can not set a new room value to the buffer\n");
			TRY_ABORT();
		}
		goto err;
	}

	return (buf);
err:
	/*@ignore@**/
	if (BUFT_OK != buf_free(buf)) {
		DE("Can not release the buffer\n");
	}
	return (NULL);
	/*@end@**/
}

ret_t buf_str_add(/*@in@*//*@temp@*/buf_t *buf, /*@in@*//*@temp@*/const char *new_data, const buf_s64_t size)
{
	size_t    new_size;
	buf_s64_t old_size;

	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	T_RET_ABORT(new_data, -EINVAL);

	old_size = buf_get_used_count(buf);

	new_size = size;
	/* If this buffer is a string buffer, we should consider \0 after string. If this buffer is empty,
	   we add +1 for the \0 terminator. If the buffer is not empty, we reuse existing \0 terminator */
	if (0 == old_size) {
		new_size += sizeof(char);
	}

	/* Add room if needed: buf_test_room() adds room if needed to hold 'new_size' bytes of additional data */
	if (0 != buf_test_room(buf, new_size)) {
		DE("Can't add room into buf_t\n");
		TRY_ABORT();
		return (-ENOMEM);
	}

	/* All done, now add new data into the buffer */
	/*@ignore@*/
	char *memory_to_copy = buf_get_data_ptr(buf);
	memory_to_copy += old_size;
	memcpy(memory_to_copy, new_data, size);
	/*@end@*/
	if (BUFT_OK != buf_inc_used(buf, size)) {
		DE("Can not increase 'used'\n");
		return (-BUFT_BAD);
	}
	BUF_TEST(buf);
	return (BUFT_OK);
}

ret_t buf_str_add_buf(/*@in@*//*@temp@*/buf_t *buf_to, buf_t *buf_from)
{
	TESTP(buf_to, -BUFT_NULL_POINTER);
	TESTP(buf_from, -BUFT_NULL_POINTER);
	TESTP(buf_from->data, -BUFT_NULL_POINTER);
	return buf_str_add(buf_to, buf_from->data, buf_from->used);
}

#if 0 /* SEB */
	#define GO_RIGHT (0)
	#define GO_LEFT (1)
/* Reimplemented as binary search */
ret_t buf_str_detect_used(/*@in@*//*@temp@*/buf_t *buf){
	buf_s64_t       step                 = 0;
	buf_s64_t calculated_used_size = 0;
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	char *buf_data = buf_get_data_ptr(buf);

	if (NULL == buf_data) {
		DE("No data in the buffer");
		return (-BUFT_NULL_POINTER);
	}

	buf_s64_t room_size = buf_get_room_count(buf);

	/* If the buf is empty - return with error */
	if (room_size < 0) {
		DE("Buffer is invalid, the room size is negative: %ld\n", room_size);
		TRY_ABORT();
		return (-ECANCELED);
	}

	/* If the buf is empty - return with error */
	if (0 == room_size) {
		DE("Tryed to detect used in an empty buffer\n");
		return (-ECANCELED);
	}

	/* Changed to 1 when the search completed */
	char      completed   = 0;

	/* Where we go next, left  or righ? */
	char      go_next     = GO_RIGHT;

	/* We start from the beginning of the buffer */
	buf_s64_t next_offset = 0;

	do {
		step++;

		DD("Step %ld, offset %ld, should go: %s\n", step, next_offset, (GO_RIGHT == go_next) ? "right" : "left");;

		/* We increase or decrease the go_next offset, every step the increase/decrease is smaller and smaller */
		if (GO_RIGHT == go_next) {
			next_offset += room_size / (step * 2);
		} else {
			next_offset -= room_size / (step * 2);
		}

		DDD(" Starting step %ld, next_offset: %ld\n", step, next_offset);

		/*** CASE 1: The current offset is 0 byte;  we should go left */

		/* Not yet used memory , we should go left */
		if (*(buf_data + next_offset) == '\0') {
			if (*(buf_data + next_offset - 1) != '\0') {
				/* We found that this is \0 after the last character */
				calculated_used_size = next_offset;
				completed = 1;

				DD("Found the end, size: %ld : %X %X\n", calculated_used_size, *(buf_data + next_offset - 1), *(buf_data + next_offset));
				break;
			}

			/* No, on the left is still 0, we should continue */
			/**/
			next_offset -= (room_size / step);
			go_next = GO_LEFT;
			continue;
		} /* End of 0 case; we contunue below if found character is not 0 */

		/*** CASE 2: The current offset is not 0 byte; we should go right */

		/* If we here, it means the next_offset points to not 0 byte; we check a byte on right */
		/* In the previous case we tested buf_data + next_offset and we know it is not 0 */
		if (*(buf_data + next_offset + 1) == 0) {
			/* We found that this is \0 after the last character */
			calculated_used_size = next_offset + 1;
			completed = 1;
			DD("Found the end, size: %ld, %X %X\n", calculated_used_size, *(buf_data + next_offset), *(buf_data + next_offset + 1));
			break;
		}

		next_offset += (room_size / step);
		go_next = GO_RIGHT;

	} while (0 == completed);

	/* If the calculated used_size is te same as set in the buffer - nothing to do */
	if (calculated_used_size == buf_get_used_count(buf)) {
		DDD0("No need new string size: %ld -> %ld\n", buf_get_used_count(buf), calculated_used_size);
		return BUFT_OK;
	}

	DDD0("Setting new string size: %ld -> %ld\n", buf_get_used_count(buf), calculated_used_size);
	/* No, the new size if less than the current */
	if (BUFT_OK != buf_set_used(buf, calculated_used_size)) {
		DE("Can not set a new 'used' value to the buffer\n");
		return (-BUFT_BAD);
	}

	DD("Found used count in %ld steps\n", step);
	return (BUFT_OK);
}

/* We don't need it anymore */
	#undef GO_RIGHT
	#undef GO_LEFT
#endif

/* TODO: Make it logarithmoc search */
#if 1 /* SEB */
ret_t buf_str_detect_used(/*@in@*//*@temp@*/buf_t *buf){
	char      *_buf_data;
	buf_s64_t calculated_used_size;
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* If the buf is empty - return with error */
	if (0 == buf_get_room_count(buf)) {
		DE("Tryed to detect used in an empty buffer\n");
		return (-ECANCELED);
	}

	/* We start with the current 'room' size */
	calculated_used_size = buf_get_room_count(buf);

	/* Search for the first NOT 0 character - this is the end of 'used' area */
	/* TODO: Replace this with a binary search:
	   We start from the end, and if there is 0 detected, we start test it towards beginning using binary search */
	while (calculated_used_size > 0) {
		/* If found not null in the buffer... */
		_buf_data = (char *)buf_get_data_ptr(buf);
		if ((char)0 != _buf_data[calculated_used_size]) {
			break;
		}
		calculated_used_size--;
	}

	/* We increase it by 1 because it is index of array, i.e. starts from 0, but we need the legth, i.e. starts from 1 */
	calculated_used_size++;

	/* If the calculated used_size is te same as set in the buffer - nothing to do */
	if (calculated_used_size == buf_get_used_count(buf)) {
		DDD0("No need new string size: %ld -> %ld\n", buf_get_used_count(buf), calculated_used_size);
		return BUFT_OK;
	}

	DDD0("Setting new string size: %ld -> %ld\n", buf_get_used_count(buf), calculated_used_size);
	/* No, the new size if less than the current */
	if (BUFT_OK != buf_set_used(buf, calculated_used_size)) {
		DE("Can not set a new 'used' value to the buffer\n");
		return (-BUFT_BAD);
	}
	return (BUFT_OK);
}
#endif


ret_t buf_str_pack(/*@temp@*//*@in@*/buf_t *buf)
{
	/*@temp@*/ char   *tmp = NULL;
	size_t new_size = -1;
	ret_t  ret;

	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	/* If the buf is empty - return with error */
	if (0 == buf_get_room_count(buf)) {
		DE("Tryed to pack an empty buffer\n");
		return (-ECANCELED);
	}

	ret = buf_str_detect_used(buf);
	if (BUFT_OK != ret) {
		return (ret);
	}

	new_size = buf_get_used_count(buf);
	/*@access buf_t@*/
	if (BUFT_YES == buf_is_canary(buf)) {
		new_size += BUF_T_CANARY_SIZE;
	}

	if (BUFT_YES == buf_is_crc(buf)) {
		new_size += BUF_T_CRC_SIZE;
	}
	/*@noaccess buf_t@*/

	new_size++;

	DDD0("Going to resize the buf room %lu -> %lu\n", buf_get_room_count(buf), new_size);

	/* TODO: Consider CRC + CANARY */

	/*@ignore@*/
	tmp = realloc(buf->data, new_size);

	/* Case 1: realloc can't reallocate */
	if (NULL == tmp) {
		DE("Realloc failed\n");
		return (-ENOMEM);
	}

	/* Case 2: realloc succeeded, new memory returned */
	/* No need to free the old memory - done by realloc */
	if (NULL != tmp) {
		buf->data = tmp;
	}
	/*@end@*/

	if (BUFT_OK != buf_set_room_count(buf, new_size)) {
		DE("Can not set a new room value to the buffer\n");
		return (-BUFT_BAD);
	}

	return (BUFT_OK);
}

/*@null@*/buf_t *buf_sprintf(/*@in@*//*@temp@*/const char *format, ...)
{
	va_list args;
	/*@temp@*//*@in@*/buf_t   *buf = NULL;
	int     rc   = -1;

	T_RET_ABORT(format, NULL);

	/* Create buf_t with reserved room for the string */
	buf = buf_string(0);
	T_RET_ABORT(buf, NULL);

	va_start(args, format);
	/* Measure string lengh */
	/*@ignore@*/
	rc = vsnprintf(NULL, 0, format, args);
	/*@end@*/
	va_end(args);

	DDD("Measured string size: it is %d\n", rc);

	/* Allocate buffer: we need +1 for final '\0' */
	rc = buf_add_room(buf, rc + 1);

	if (BUFT_OK != rc) {
		DE("Can't add room to buf\n");
		if (BUFT_OK != buf_free(buf)) {
			DE("Warning, can't free buf_t, possible memory leak\n");
		}
		return (NULL);
	}
	va_start(args, format);
	rc = vsnprintf(buf_get_data_ptr(buf), buf_get_room_count(buf), format, args);
	va_end(args);

	if (rc < 0) {
		DE("Can't print string\n");
		if (BUFT_OK != buf_free(buf)) {
			DE("Warning, can't free buf_t, possible memory leak\n");
		}
		return (NULL);
	}

	if (BUFT_OK != buf_set_used(buf, buf_get_room_count(buf) - 1)) {
		DE("Can not set a new 'used' value to the buffer\n");
		if (BUFT_OK != buf_free(buf)) {
			DE("Warning, can't free buf_t, possible memory leak\n");
		}
		return (NULL);
	}

	if (BUFT_OK != buf_is_valid(buf)) {
		DE("Buffer is invalid - free and return\n");
		TRY_ABORT();
		if (BUFT_OK != buf_free(buf)) {
			DE("Can not crelease a buffer\n");
		}
		return (NULL);
	}

	return (buf);
}

ret_t buf_str_concat(/*@in@*//*@temp@*//*notnull*/buf_t *dst, /*@in@*//*@temp@*//*notnull*/buf_t *src)
{
	char *_dst_buf_data;
	char *_src_buf_data;
	T_RET_ABORT(src, -EINVAL);
	T_RET_ABORT(dst, -EINVAL);

	if (BUFT_OK != buf_type_is_string(src)) {
		DE("src buffer is not string\n");
		TRY_ABORT();
		return -BUFT_BAD;
	}

	if (BUFT_OK != buf_type_is_string(dst)) {
		DE("dst buffer is not string\n");
		TRY_ABORT();
		return -BUFT_BAD;
	}

	if (BUFT_OK != buf_add_room(dst, buf_get_used_count(src))) {
		DE("Can not add room for string copy");
	}

	_dst_buf_data = (char *)buf_get_data_ptr(dst);
	_src_buf_data = (char *)buf_get_data_ptr(src);
	memcpy(_dst_buf_data + buf_get_used_count(dst), _src_buf_data, buf_get_used_count(src));
	if (BUFT_OK != buf_inc_used(dst, buf_get_used_count(src))) {
		DE("Can not increase 'used'\n");
		TRY_ABORT();
		return -BUFT_BAD;
	}
	//dst->used += buf_used(src);
	_dst_buf_data[buf_get_used_count(dst)] = '\0';
	return BUFT_OK;
}

