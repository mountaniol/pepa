/** @file */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <netdb.h>
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/errno.h>

#include "buf_t.h"
#include "buf_t_stats.h"
#include "buf_t_debug.h"
#include "buf_t_memory.h"
#include "se_tests.h"

#define TMP_BUF_SIZE (1024)

/* Abort on error */
static buf_t_flags_t g_abort_on_err = ABORT_ON_ERROR_OFF;

int bug_get_abort_flag(void)
{
	return g_abort_on_err;
}

/* Flags: set on every new buffer */
static buf_t_flags_t g_flags;

buf_t_flags_t buf_save_flags(void)
{
	return (g_flags);
}

void buf_restore_flags(buf_t_flags_t flags)
{
	g_flags = flags;
}

void buf_set_abort_flag(void)
{
	DDD("buf_t: enabled 'abort on error' state\n");
	g_abort_on_err = ABORT_ON_ERROR_ON;
}

void buf_unset_abort_flag(void)
{
	DDD("buf_t: disabled 'abort on error' state\n");
	g_abort_on_err = ABORT_ON_ERROR_OFF;
}

/***** Flags section */

/* Set default flags for all further bufs */
void buf_default_flags(buf_t_flags_t f)
{
	g_flags |= f;
}

/* Set flag(s) of the buf */
ret_t buf_set_flag(/*@temp@*//*@in@*/buf_t *buf, buf_t_flags_t f)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	buf->flags |= f;
	return (BUFT_OK);
}

/* Clear flag(s) of the buf */
ret_t buf_rm_flag(/*@temp@*//*@in@*/buf_t *buf, buf_t_flags_t f)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	buf->flags &= ~f;
	return (BUFT_OK);
}

/* Set flag(s) of the buf */
ret_t buf_set_type(/*@temp@*//*@in@*/buf_t *buf, buf_t_type_t t)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	buf->type = t;
	return (BUFT_OK);
}

/* Clear flag(s) of the buf */
buf_t_flags_t buf_get_type(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, BUFT_CHAR_ERR);
	return buf->type;
}


/***** Set of functions to add a flag to the buffer */

ret_t buf_set_type_string(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_type(buf, BUF_TYPE_STRING));
}

ret_t buf_set_type_array(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_type(buf, BUF_TYPE_ARR));
}

ret_t buf_mark_immutable(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_flag(buf, BUF_FLAG_IMMUTABLE));
}

ret_t buf_mark_locked(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_flag(buf, BUF_FLAG_LOCKED));
}

ret_t buf_lock(/*@temp@*//*@in@*/buf_t *buf)
{
	return buf_mark_locked(buf);
}

ret_t buf_mark_compresed(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_flag(buf, BUF_FLAG_COMPRESSED));
}

ret_t buf_mark_encrypted(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_flag(buf, BUF_FLAG_ENCRYPTED));
}

ret_t buf_mark_canary(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_flag(buf, BUF_FLAG_CANARY));
}

ret_t buf_mark_crc(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_flag(buf, BUF_FLAG_CRC));
}

ret_t buf_mark_fixed(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_set_flag(buf, BUF_FLAG_FIXED_SIZE));
}

/***   */

/**
 * @author Sebastian Mountaniol (11/19/23)
 * @brief Test if the buffer internal manipulations are allowed.
  They are not allowed for immutable or locked buffer
 * @param buf_t* buf   
 * @return ret_t 
 * @details 
 */
ret_t buf_is_change_allowed(buf_t *buf)
{
	/* We can't add room to a constant buffer */
	if (BUFT_YES == buf_is_immutable(buf)) {
		DE("Warning: tried add room to Read-Only buffer\n");
		TRY_ABORT();
		return (-BUFT_IS_IMMUTABLE);
	}

	/* We can't add room to a locked buffer */
	if (BUFT_YES == buf_is_locked(buf)) {
		DE("Warning: tried add room to a locked buffer\n");
		TRY_ABORT();
		return (-BUFT_IS_LOCKED);
	}

	return (BUFT_YES);
}

/***** Set of functions to remove a flag from the buffer */

ret_t buf_unmark_immutable(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_rm_flag(buf, BUF_FLAG_IMMUTABLE));
}

ret_t buf_unmark_locked(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_rm_flag(buf, BUF_FLAG_LOCKED));
}

ret_t buf_unlock(/*@temp@*//*@in@*/buf_t *buf)
{
	return buf_unmark_locked(buf);
}

ret_t buf_unmark_compressed(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_rm_flag(buf, BUF_FLAG_COMPRESSED));
}

ret_t buf_unmark_encrypted(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_rm_flag(buf, BUF_FLAG_ENCRYPTED));
}

ret_t buf_unmark_canary(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_rm_flag(buf, BUF_FLAG_CANARY));
}

ret_t buf_unmark_crc(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_rm_flag(buf, BUF_FLAG_CRC));
}

ret_t buf_unmark_fixed(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf_rm_flag(buf, BUF_FLAG_FIXED_SIZE));
}

/* Set canary word in the end of the buf
 * If buf has 'BUF_T_CANARY' flag set, it means
 * that extra space for canary pattern is reserved
 * in the end of the buf->data
 */
ret_t buf_set_canary(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	buf_t_canary_t canary;
	buf_t_canary_t *canary_p;
	TESTP_BUF_DATA(buf);
	if (BUFT_NO == buf_is_canary(buf)) {
		DE("The buffer doesn't have CANARY flag\n");
		TRY_ABORT();
		/*@ignore@*/
		return (-BUFT_NO_CANARY);
		/*@end@*/
	}

	canary = BUF_T_CANARY_CHAR_PATTERN;
	canary_p = (buf_t_canary_t *)(buf->data + buf_get_room_count(buf));
	memcpy(canary_p, &canary, BUF_T_CANARY_SIZE);

	/* Test that the canary pattern set */
	if (0 != memcmp(canary_p, &canary, BUF_T_CANARY_SIZE)) {
		DE("Can't set CANARY\n");
		TRY_ABORT();
		return (-BUFT_CANNOT_SET_CANARY);
	}
	return (BUFT_OK);
}

buf_s64_t buf_get_used_count(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* In case it is a circ buffer */
	if (BUFT_OK == buf_type_is_circ(buf)) {
		DD("The buffer is CIRC\n");
		if (buf->ht.head <= buf->ht.tail) {
			return (buf->ht.head - buf->ht.tail);
		}
		return (buf_get_room_count(buf) - (buf->ht.head - buf->ht.tail));
	}

	/* In case it is an array buffer */
	if (BUFT_OK == buf_type_is_array(buf)) {
		return buf_arr_get_used(buf);
	}

	/* In other cases, just return the '->used' field */
	return (buf->used);
}

ret_t buf_set_used(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t used)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* In case it is a circ buffer */
	if (BUFT_OK == buf_type_is_circ(buf)) {
		DE("Can not set ->used for CIRC fuffer - use 'buf_set_head_tail()' instead\n");
		abort();
	}

	/* In case it is an array buffer */
	if (BUFT_OK == buf_type_is_array(buf)) {
		return buf_array_set_used(buf, used);
	}

	buf->used = used;
	return (BUFT_OK);
}

ret_t buf_inc_used(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t inc)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* In case it is a circ buffer, */
	if (BUFT_OK == buf_type_is_circ(buf)) {
		DE("Can not add to ->used for CIRC fuffer - use 'buf_add_head_tail()' instead\n");
		TRY_ABORT();
		return (-BUFT_BAD_BUFT_TYPE);
	}

	/* In case of array buffer it is not applicable */
	if (BUFT_OK == buf_type_is_array(buf)) {
		DE("Can not add to ->used for ARRAY fuffer - use 'buf_add_head_tail()' instead\n");
		TRY_ABORT();
		return (-BUFT_BAD_BUFT_TYPE);
	}

	/* TODO: Detext out of limit situation */
	buf->used += inc;
	return (BUFT_OK);
}

ret_t buf_dec_used(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t dec)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* In case it is a circ buffer, */
	if (BUFT_OK == buf_type_is_circ(buf)) {
		DE("Can not add to ->used for CIRC fuffer - use 'buf_add_head_tail()' instead\n");
		TRY_ABORT();
		return (-BUFT_BAD_BUFT_TYPE);
	}

	/* In case of array buffer it is not applicable */
	if (BUFT_OK == buf_type_is_array(buf)) {
		DE("Can not add to ->used for ARRAY fuffer - use 'buf_add_head_tail()' instead\n");
		TRY_ABORT();
		return (-BUFT_BAD_BUFT_TYPE);
	}

	if (dec > buf_get_used_count(buf)) {
		DE("Can not decrement buf->used: the dec > buf->used (%ld > %ld)\n",
		   dec, buf_get_used_count(buf));
		return (-BUFT_OUT_OF_LIMIT_OP);
	}

	if (buf->used >= dec) {
		buf->used -= dec;
	}
	return BUFT_OK;
}

/***** Test buffer flags *****/

static ret_t buf_has_flag(buf_t *buf, int flag)
{
	if (buf->flags & flag) {
		return BUFT_YES;
	}

	return BUFT_NO;
}

ret_t buf_is_immutable(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
{
	return buf_has_flag(buf, BUF_FLAG_IMMUTABLE);
}

ret_t buf_is_compressed(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
{
	return buf_has_flag(buf, BUF_FLAG_COMPRESSED);
}

ret_t buf_is_encrypted(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
{
	return buf_has_flag(buf, BUF_FLAG_ENCRYPTED);
}

ret_t buf_is_canary(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
{
	return buf_has_flag(buf, BUF_FLAG_CANARY);
}

ret_t buf_is_crc(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
{
	return buf_has_flag(buf, BUF_FLAG_CRC);
}

ret_t buf_is_fixed(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
{
	return buf_has_flag(buf, BUF_FLAG_FIXED_SIZE);
}

ret_t buf_is_locked(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
{
	return buf_has_flag(buf, BUF_FLAG_LOCKED);
}


/* This function will add canary bits after an existing buffer
 * and add the CANARY flag. The buffer room will be decreased by size of canary.
 * If buf->used == buf->room, the ->used be decreased as well.
 * If this buffer contains a string, i.e.type is BUF_T_STRING,
 * a '\0' will be added before the canary bits
 */
ret_t buf_force_canary(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* In case there is no space fpr CANARY tail,
	   try to increase buffer space to keep CANARY */

	if (buf_get_used_count(buf) < (buf_s64_t)BUF_T_CANARY_SIZE ||
		buf_get_used_count(buf) == buf_get_room_count(buf)) {

		/* Add room to keep CANARY tail */
		int rc = buf_add_room(buf, BUF_T_CANARY_SIZE);
		if (BUFT_OK != rc) {
			DE("Could not add room\n");
			return (rc);
		}
	}

	/* If there no space to set CANARY tail, we abort the function */
	if (buf_get_used_count(buf) < (buf_s64_t)BUF_T_CANARY_SIZE) {
		DE("Buffer is to small for CANARY word\n");
		TRY_ABORT();
		return (-BUFT_TOO_SMALL);
	}

	/* The buffer is big enough but all allocated data is occupied */
	if (buf_get_used_count(buf) == buf_get_room_count(buf)) {
		if (BUFT_OK != buf_dec_used(buf, BUF_T_CANARY_SIZE)) {
			DE("Can not decrement a buffer used value");
			return (-BUFT_ALLOCATE);
		}
	}

	if (BUFT_OK != buf_dec_room_count(buf, BUF_T_CANARY_SIZE)) {
		DE("Can not decrement a buffer room value\n");
		return (-BUFT_DECREMENT);
	}
	return (buf_set_canary(buf));
}

/* If CANARY (a pattern after the buf->data) enabled we test its integrity */
ret_t buf_test_canary(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	buf_t_canary_t canary = BUF_T_CANARY_CHAR_PATTERN;
	TESTP_BUF_DATA(buf);

	/* If this buf doesn't have CANARY flag, we stop the test */
	if (BUFT_NO == buf_is_canary(buf)) {
		return (-BUFT_NO_CANARY);
	}

	if (0 == memcmp(buf->data + buf_get_room_count(buf), &canary, BUF_T_CANARY_SIZE)) {
		return (BUFT_OK);
	}

	DE("The buf CANARY word is wrong, expected: %X, current: %X\n", BUF_T_CANARY_CHAR_PATTERN, (unsigned int)*(buf->data + buf_get_room_count(buf)));

	TRY_ABORT();
	return (-BUFT_BAD_CANARY);
}

/* Extract canary word from the buf */
buf_t_canary_t buf_get_canary(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	buf_t_canary_t *canary_p;
	T_RET_ABORT(buf, -1);
	if (BUFT_NO == buf_is_canary(buf)) {
		DE("The buffer doesn't have canary flag\n");
		return (-1);
	}

	//memcpy(&canary, buf->data + buf->room, BUF_T_CANARY_SIZE);
	canary_p = (buf_t_canary_t *)(buf->data + buf_get_room_count(buf));
	return (*canary_p);
}

void buf_print_flags(/*@temp@*//*@in@*/buf_t *buf)
{
	TESTP_VOID(buf);
	switch (buf_get_type(buf)) {
	case BUF_TYPE_STRING:
		DDD("Buffer is STRING\n");
		break;
	case BUF_TYPE_BIT:
		DDD("Buffer is BIT\n");
		break;
	case BUF_TYPE_CIRC:
		DDD("Buffer is CIRC\n");
		break;
	case BUF_TYPE_ARR:
		DDD("Buffer is ARRAY\n");
		break;
	default:
		DDD("Buffer is unknown type\n");
		TRY_ABORT();
	}
	if (BUFT_YES == buf_is_immutable(buf)) DDD("Buffer is READONLY\n");
	if (BUFT_YES == buf_is_compressed(buf)) DDD("Buffer is COMPRESSED\n");
	if (BUFT_YES == buf_is_encrypted(buf)) DDD("Buffer is ENCRYPTED\n");
	if (BUFT_YES == buf_is_canary(buf)) DDD("Buffer is CANARY\n");
	if (BUFT_YES == buf_is_crc(buf)) DDD("Buffer is CRC\n");
	if (BUFT_YES == buf_is_locked(buf)) DDD("Buffer is locked\n");
}

/* Validate sanity of buf_t - common for all buffers */
static ret_t buf_common_is_valid(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* buf->used always <= buf->room */
	/* TODO: not in case of CIRC buffer */
	if (buf_get_used_count(buf) > buf_get_room_count(buf)) {
		DE("Invalid buf: buf->used > buf->room\n");
		TRY_ABORT();
		return (-BUFT_BAD_USED);
	}

	/* The buf->data may be NULL if and only if both buf->used and buf->room == 0; However, we don't
	   check buf->used: we tested that it <= buf->room already */
	if ((NULL == buf->data) && (buf_get_room_count(buf) > 0)) {
		DE("Invalid buf: buf->data == NULL but buf->room > 0 (%ld)\n", buf_get_room_count(buf));
		TRY_ABORT();
		return (-BUFT_NULL_DATA);
	}

	/* And vice versa: if buf->data != NULL the buf->room must be > 0 */
	if ((NULL != buf->data) && (0 == buf_get_room_count(buf))) {
		DE("Invalid buf: buf->data != NULL but buf->room == 0\n");
		TRY_ABORT();
		return (-BUFT_BAD_ROOM);
	}

	/* If the buffer have canary, test it; if the canary is bad, stop */
	if (buf_get_room_count(buf) > 0 &&
		(BUFT_YES == buf_is_canary(buf)) &&
		(BUFT_OK != buf_test_canary(buf))) {
#ifdef DE
		buf_t_canary_t *canary_p = (buf_t_canary_t *)buf->data + buf_get_room_count(buf);
#endif
		DE("The buffer was overwritten: canary word is wrong\n");
		DE("Expected canary: %X, current canary: %X\n", BUF_T_CANARY_CHAR_PATTERN, *canary_p);
		TRY_ABORT();
		return (-BUFT_BAD_CANARY);
	}

	/* TODO: Is it really a wrong situation? We can lovk and unlock the buffer */
	/* In Read-Only buffer buf->room must be == bub->used */
	if ((BUFT_YES == buf_is_immutable(buf)) &&
		(buf_get_room_count(buf) != buf_get_used_count(buf))) {
		DE("Warning: in Read-Only buffer buf->used (%ld) != buf->room (%ld)\n", buf_get_used_count(buf), buf_get_room_count(buf));
		TRY_ABORT();
		return (-BUFT_IMMUTABLE_DAMAGED);
	}

	DDD0("Buffer is valid\n");
	//buf_print_flags(buf);
	return (BUFT_OK);
}

ret_t buf_is_valid(/*@temp@*//*@in@*/buf_t *buf)
{
	ret_t ret;
	ret = buf_common_is_valid(buf);
	if (BUFT_OK != ret) {
		DE("Buffer is invalud - returning/aborting");
		TRY_ABORT();
		return ret;
	}

	switch (buf_get_type(buf)) {
	case BUF_TYPE_STRING:
		return buf_str_is_valid(buf);
	case BUF_TYPE_ARR:
		return buf_array_is_valid(buf);
	}
	return (ret);
}

/***** TEST BUFFER TYPE ******/

int buf_type_is_raw(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	if (0 == buf_get_type(buf)) {
		return (BUFT_YES);
	}
	return (BUFT_NO);
}

int buf_type_is_string(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	if (BUF_TYPE_STRING == buf_get_type(buf)) {
		return (BUFT_YES);
	}
	return (BUFT_NO);
}

int buf_type_is_array(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	if (BUF_TYPE_ARR == buf_get_type(buf)) {
		return (BUFT_YES);
	}
	return (BUFT_NO);
}

int buf_type_is_bit(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	if (BUF_TYPE_STRING == buf_get_type(buf)) {
		return (BUFT_YES);
	}
	return (BUFT_NO);
}

int buf_type_is_circ(/*@temp@*//*@in@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	if (BUF_TYPE_CIRC == buf_get_type(buf)) {
		return (BUFT_YES);
	}
	return (BUFT_NO);
}

/*@only@*//*@in@*/ buf_t *buf_new(buf_s64_t size)
{
	/*@temp@*/buf_t *buf;

	/* The real size of allocated  buffer can be more than used asked,
	   the canary and the checksum space could be added */
	size_t real_size = size;

	buf = zmalloc(sizeof(buf_t));
	T_RET_ABORT(buf, NULL);

	/* Increase statistics of allocated buffers */
	buf_allocs_num_inc();

	/* Set global flags for the start */
	buf->flags = g_flags;

	/* If a size is given than allocate a data */
	if (size > 0) {

		/* If CANARY is set in global flags add space for CANARY word */
		if (BUFT_YES == buf_is_canary(buf)) {
			real_size += BUF_T_CANARY_SIZE;
		}

		/* TODO: If CRC flag set, allocale also space for CRC */

		buf->data = zmalloc(real_size);
		TESTP_ASSERT(buf->data, "Can't allocate buf->data");
	}

	/* TODO: in case of CIRC buffer it is wrong */
	if (BUFT_OK != buf_set_room_count(buf, size)) {
		DE("Can not set a new 'room' value\n");
		free(buf);
		return (NULL);
	}

	if (BUFT_OK != buf_set_used(buf, 0)) {
		DE("Can not set a new 'used' value\n");
		free(buf);
		return (NULL);
	}

	/* Set CANARY word */
	if (size > 0 &&
		(BUFT_YES == buf_is_canary(buf)) &&
		BUFT_OK != buf_set_canary(buf)) {
		DE("Can't set CANARY word\n");
		if (BUFT_OK != buf_free(buf)) {
			DE("Can't free the buffer\n");
		}
		TRY_ABORT();
		return (NULL);
	}

	if (BUFT_OK != buf_is_valid(buf)) {
		DE("Buffer is invalid right after allocation!\n");
		if (BUFT_OK != buf_free(buf)) {
			DE("Can not free the buffer\n");
		}
		TRY_ABORT();
		return (NULL);
	}

	return (buf);
}

ret_t buf_set_data(/*@temp@*//*@in@*//*@special@*/buf_t *buf, /*@null@*/ /*@only@*/ /*@in@*/char *data, const buf_s64_t size, const buf_s64_t len)
/*@sets buf->data@*/
{
	ret_t rc = 0;
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	T_RET_ABORT(data, -BUFT_NULL_POINTER);

	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	if (BUFT_YES == buf_is_canary(buf)) {
		DE("The buffer has CANARY flag; it must be unset before replacinf data\n");
		return (-BUFT_HAS_CANARY);
	}

	buf->data = data;
	rc = buf_set_room_count(buf, size);
	if (BUFT_OK != rc) {
		DE("Can not set a new value to the buffer\n");
		return (rc);
	}

	rc = buf_set_used(buf, len);
	if (BUFT_OK != rc) {
		DE("Can not set a new 'used' value to the buffer\n");
		return (rc);
	}

	return (BUFT_OK);
}

/* Set data into read-only buffer: no changes allowed after that */
ret_t buf_set_data_immutable(/*@temp@*//*@in@*//*@special@*/buf_t *buf, /*@null@*//*@only@*//*@in@*/char *data, buf_s64_t size)
/*@sets buf->data@*/
{
	int rc;
	TESTP_BUF_DATA(buf);

	rc = buf_set_data(buf, data, size, size);
	if (BUFT_OK != rc) {
		DE("Can't set data\n");
		return (rc);
	}

	rc = buf_unmark_canary(buf);
	if (BUFT_OK != rc) {
		DE("Can not unset CANARY flag\n");
		return (rc);
	}

	rc = buf_mark_immutable(buf);
	if (BUFT_OK != rc) {
		DE("Can not set RO flag\n");
		return (rc);
	}
	return (BUFT_OK);
}

/*@null@*//*@only@*/void *buf_steal_data(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	int rc;
	/*@temp@*/void *data;
	T_RET_ABORT(buf, NULL);
	data = buf->data;
	buf->data = NULL;

	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return (NULL);
	}

	if (BUFT_OK != buf_set_room_count(buf, 0)) {
		DE("Can not set a new value to the buffer\n");
		TRY_ABORT();
		return (data);
	}

	if (BUFT_OK != buf_set_used(buf, 0)) {
		DE("Can not set a new 'used' value to the buffer\n");
		TRY_ABORT();
		return (data);
	}
	/* TODO: If CANARY used - zero it, dont reallocate the buffer */
	return (data);
}

/*@null@*/void *buf_to_data(/*@only@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
/*@releases buf@*/
{
	void *data;
	T_RET_ABORT(buf, NULL);
	/* This function is force to extract data and destroy the buf_t struct.
	   I the buffer is unmutable, we remove this flag */

	if (BUFT_YES == buf_is_immutable(buf)) {
		int rc = buf_unmark_immutable(buf);
		if (BUFT_OK != rc) {
			DE("Could not unset IMMUTABLE flag\n");
			TRY_ABORT();
			return (NULL);
		}
	}

	/* If the buffer is locked, we stop the operation, it must be unlocked first */
	if (BUFT_YES == buf_is_locked(buf)) {
		DE("Can not steal data from a locked buffer; unlock it first\n");
		TRY_ABORT();
		return (NULL);
	}

	data = buf_steal_data(buf);

	if (BUFT_OK != buf_free(buf)) {
		DE("Warning! Memory leak: can't clean buf_t!");
		TRY_ABORT();
	}

	return (data);
}

/* TODO: add an 'err' aviable, this function can return NULL if there no data, but also returns NULL on an error */
void /*@temp@*//*@null@*/ *buf_get_data_ptr(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	/* If buf is invalid we return '-1' costed into uint */
	T_RET_ABORT(buf, NULL);
	return (buf->data);
}

ret_t buf_data_is_null(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	/* If buf is invalid we return '-1' costed into uint */
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	if (NULL == buf->data) {
		return BUFT_YES;
	}
	return BUFT_NO;
}


/*
 * Accordding to researches, realloc() should be used only for the
 * buffer decreasing. It shows much more efficient execution.
 * However, if the buffer increased, the malloc() + memcpy()
 * is more efficient in most cases.
 * https://copyprogramming.com/howto/differences-between-using-realloc-vs-free-malloc-functions
 */
/**
 * @author Sebastian Mountaniol (11/23/23)
 * @brief Increase size of the buffer using malloc() + memcpy()
 * @param buf_t* buf     Buffer to increase the internal buffer size
 * @param buf_s64_t new_size New size (absolute) to allocate
 * @return ret_t BUFT_OK on success, a negative error code on an error
 * @details This function only allocate memory
 * and copy the content of the old buffer into the new one.
 * It does not modify values of ->used and ->room, the called must do it.
 */
static ret_t buf_increase(/*@temp@*//*@in@*//*@special@*/buf_t *buf, buf_s64_t new_size)
{
	buf_s64_t size_to_clean = 0;

	TESTP(buf, -BUFT_NULL_POINTER);
	buf_s64_t buf_used_size = buf_get_used_count(buf);
	buf_s64_t buf_room_size = buf_get_room_count(buf);

	if (buf_room_size >= new_size) {
		DE("This function supposed to increase memory; the asked used memory size (%ld) <= existing (%ld)\n",
		   new_size, buf_room_size);
		return (-BUFT_BAD_SIZE);
	}

	/* Allocate the new buffer */
	char *tmp = zmalloc(new_size);

	if (NULL == tmp) {
		DE("Could not allocate a new buffer, asked size is %ld\n", new_size);
		return (-BUFT_ALLOCATE);
	}

	/* Copy only used data from the old buffer data to the new memory */
	if (buf_used_size > 0) {
		memcpy(tmp, buf->data, buf_used_size);
	}

	/* Calculate what is the size of the new buffer tail to be filled with 0 */
	size_to_clean = new_size - buf_used_size;

	if (size_to_clean > 0) {
		/* Clean the tail of the new buffer, it starts from the ofssed buf_used */
		memset(tmp + buf_used_size, 0, size_to_clean);
	}

	/* If there is memory in the buffer, release it */
	if (NULL != buf->data) {
		/* Considering possible security issues, we fill the old data with zeros */
		memset(buf->data, 0, buf->room);
		free(buf->data);
	}

	buf->data = tmp;
	return BUFT_OK;
}

/**
 * @author Sebastian Mountaniol (11/19/23)
 * @brief Internal function to change ->data size 
 * @param buf_t* buf     Buffer to realloc data
 * @param size_t new_size New size of data
 * @return ret_t BUFT_OK on success
 * @details This is the internal function; we do not test
 *  		wheather the buffer is immutable or locked.
 *  		All there tests should performed before.
 *  		WARNING: This function does not change value of
 *  		buf->room or buf->used
 */
static ret_t buf_resize(/*@temp@*//*@in@*//*@special@*/buf_t *buf, buf_s64_t new_size)
/*@allocates buf->data@*/
{
	TESTP(buf, -BUFT_NULL_POINTER);
	void      *tmp             = NULL;
	buf_s64_t buf_room_current = buf_get_room_count(buf);

	/* A special case: new data size if the same as current, we should do nothing */
	if (new_size == buf_room_current) {
		return (BUFT_OK);
	}

	/* A mini test of the buffer validity:
	 * if the buffer ->data is NULL but ->room is > 0,
	   it is not a normal situation  */
	if ((NULL == buf_get_data_ptr(buf)) && (buf_room_current > 0)) {
		return (-BUFT_BAD_ROOM);
	}

	/* We use malloc() + memcpy() in case of buffer new size > current size
	   See comments before buf_increase() */
	if (buf_room_current < new_size) {
		return buf_increase(buf, new_size);
	}

	/* A special case: if the new size == 0, we just remove the data */
	if (0 == new_size) {
		free(buf->data);
		buf->data = 0;
		return BUFT_OK;
	}

	/* If we are here, it means that 0 < (new size) < (existing size) */
	tmp = realloc(buf->data, new_size);

	/*
	 * Note:
	 * realloc() always returns a valid pointer in case of success.
	 * The pointer can point to the same memory where currentrly placed ->data,
	 * ot it can point to another memory buffer.
	 * However, it returns NULL only in case of aan error
	 */

	/*
	 * On error: realloc can't reallocate.
	 * According to 'man realloc':
	 * "If realloc() fails, the original block is left untouched; it is not freed or moved"
	 */
	if (NULL == tmp) {
		DE("Realloc failed\n");
		return (-BUFT_ALLOCATE);
	}

	/* Here: a valid pointer is returned, reassign it to the buf->data */
	buf->data = tmp;
	return BUFT_OK;
}

ret_t buf_add_room(/*@temp@*//*@in@*//*@special@*/buf_t *buf, buf_s64_t size)
/*@allocates buf->data@*/
/*@uses buf->data@*/
{
	int    rc;
	size_t canary = 0;

	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	if (0 == size) {
		DE("Bad arguments: buf == NULL (%p) or size == 0 (%lu)\b", (void *)buf, size);
		TRY_ABORT();
		return (-BUFT_BAD_SIZE);
	}

	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	if (BUFT_YES == buf_is_canary(buf)) {
		canary = BUF_T_CANARY_SIZE;
	}

	if (BUFT_OK != buf_resize(buf, buf_get_room_count(buf) + size + canary)) {
		DE("Can not reallocate buf->data\n");
		return (-BUFT_ALLOCATE);
	}

	/* Clean newely allocated memory */
	memset(buf->data + buf_get_room_count(buf), 0, size + canary);

	/* Case 3: realloc succeeded, the same pointer - we do nothing */
	/* <Beeep> */

	if (BUFT_OK != buf_inc_room_count(buf, size)) {
		DE("Can not increment 'room' value\n");
		return (-BUFT_INCREMENT);
	}

	/* If the buffer use canary add it to the end */

	if (BUFT_YES == buf_is_canary(buf) && BUFT_OK != buf_set_canary(buf)) {
		DE("Can't set CANARY\b");
		TRY_ABORT();
		/*@ignore@*/
		return (-BUFT_CANNOT_SET_CANARY);
		/*@end@*/
	}

	BUF_TEST(buf);
	return (BUFT_OK);
}

ret_t buf_test_room(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t expect)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	if (expect == 0) {
		DE("expected size == 0\n");
		TRY_ABORT();
		return (-BUFT_BAD_SIZE);
	}

	if (buf_get_used_count(buf) + expect <= buf_get_room_count(buf)) {
		return (BUFT_OK);
	}

	return (buf_add_room(buf, expect));
}

ret_t buf_fill_with_zeros(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	char      *data_ptr  = buf_get_data_ptr(buf);
	buf_s64_t room_count = buf_get_room_count(buf);

	if (NULL == data_ptr) {
		DE("Buffer does not have data\n");
		return (-BUFT_NULL_DATA);
	}

	if (0 == room_count) {
		DE("Buff data is not NULL but buf room count is 0\n");
		return buf_is_valid(buf);
	}

	memset(data_ptr, 0, room_count);
	return BUFT_OK;
}

ret_t buf_clean(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@releases buf->data@*/
{
	int rc;
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* If buffer is invalid, it can signal memory corruption */
	rc = buf_is_valid(buf);
	if (BUFT_OK != rc) {
		DE("Warning: buffer is invalid\n");
		TRY_ABORT();
		return (rc);
	}

	/* If the buffer is locked, we stop the operation, it must be unlocked first */
	if (BUFT_YES == buf_is_locked(buf)) {
		DE("Can not clean a locked buffer; unlock it first\n");
		TRY_ABORT();
		return (-BUFT_IS_LOCKED);
	}

	if (NULL != buf->data) {
		/* Security: zero memory before it freed */
		memset(buf->data, 0, buf_get_room_count(buf));
		free(buf->data);
		buf->data = NULL;
	}

	if (BUFT_OK != buf_set_room_count(buf, 0)) {
		DE("Can not set a new value to the buffer\n");
		return (-BUFT_SET_ROOM_SIZE);
	}

	if (BUFT_OK != buf_set_used(buf, 0)) {
		DE("Can not set a new 'used' value to the buffer\n");
		return (-BUFT_SET_USED_SIZE);
	}

	return (BUFT_OK);
}

ret_t buf_reset(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@releases buf->data@*/
{
	int rc = buf_clean(buf);
	if (BUFT_OK != rc) {
		DE("Could not clean the buffer\n");
		TRY_ABORT();
		return rc;
	}

	rc = buf_set_flag(buf, 0);
	if (BUFT_OK != rc) {
		DE("Could not reset the buffer flags\n");
		TRY_ABORT();
		return rc;
	}

	rc = buf_set_type(buf, BUF_TYPE_RAW);
	if (BUFT_OK != rc) {
		DE("Could not reset the buffer flags\n");
		TRY_ABORT();
		return rc;
	}

	return (BUFT_OK);
}

ret_t buf_free(/*@only@*//*@in@*//*@special@*/buf_t *buf)
/*@releases buf->data@*/
{
	int rc;
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	if (BUFT_OK != buf_is_valid(buf)) {
		DE("Warning: buffer is invalid; the freeing is being to proceed\n");
		TRY_ABORT();
	}

	/* If we asked to clean a locked buffer, we return an error.
	   The user must unlock it before it can be freed */
	rc = buf_is_locked(buf);
	if (BUFT_YES == rc) {
		DE("Asked to free a locked buffer\n");
		TRY_ABORT();
		return (rc);
	}

	rc = buf_clean(buf);
	if (BUFT_OK != rc) {
		DE("Can't clean buffer, stopped operation, returning error\n");
		TRY_ABORT();
		return (rc);
	}

	TFREE_SIZE(buf, sizeof(buf_t));

	/* The buffer is released. Write down statistics. */
	buf_release_num_inc();
	return (BUFT_OK);
}

/*
 * buf can not be NULL
 * new_data can not be NULL 				  *
 *
	*/
ret_t buf_add(/*@temp@*//*@in@*//*@special@*/buf_t *buf, /*@temp@*//*@in@*/const char *new_data, const buf_s64_t size)
/*@defines buf->data@*/
/*@uses buf->data@*/
{
	int    rc;
	size_t new_size;
	TESTP_ASSERT(buf, "buf is NULL");
	TESTP_ASSERT(new_data, "buf is NULL");
	if (size < 1) {
		/*@ignore@*/
		DE("Wrong argument(s): b = %p, buf = %p, size = %lu\n", (void *)buf, (void *)new_data, size);
		/*@end@*/
		TRY_ABORT();
		return (-BUFT_BAD_SIZE);
	}

	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	/* Here we switch to a type-specific function */
	switch (buf_get_type(buf)) {
	case BUF_TYPE_STRING:
		return buf_str_add(buf, new_data, size);
	case BUF_TYPE_ARR:
		return buf_arr_add_memory(buf, new_data, size);
	}

	new_size = size;

	/* Add room if needed */
	if (0 != buf_test_room(buf, new_size)) {
		DE("Can't add room into buf_t\n");
		TRY_ABORT();
		return (-ENOMEM);
	}

	/*@ignore@*/
	memcpy(buf->data + buf_get_used_count(buf), new_data, size);
	/*@end@*/
	if (BUFT_OK != buf_inc_used(buf, size)) {
		DE("Can not increment 'used' of a budder\n");
		return (-BUFT_BAD);
	}

	BUF_TEST(buf);
	return (BUFT_OK);
}

buf_s64_t buf_get_room_count(/*@temp@*//*@in@*/buf_t *buf)
{
	/* If buf is invalid we return '-1' costed into uint */
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	return (buf->room);
}

ret_t buf_set_room_count(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t room)
{
	int rc;
	/* If buf is invalid we return '-1' costed into uint */
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	buf->room = room;
	return BUFT_OK;
}

ret_t buf_inc_room_count(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t inc)
{
	int rc;
	/* If buf is invalid we return '-1' costed into uint */
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	buf->room += inc;
	return (BUFT_OK);
}

ret_t buf_dec_room_count(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t dec)
{
	int rc;
	/* If buf is invalid we return '-1' costed into uint */
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);
	if (dec > buf_get_room_count(buf)) {
		DE("Can'r decrement the room: dec > buf->room (%ld > %ld)\n", dec, buf_get_room_count(buf));
		return -BUFT_BAD;
	}

	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	buf->room -= dec;
	return (BUFT_OK);
}

ret_t buf_pack(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	int    rc;
	size_t new_size = -1;

	TESTP_BUF_DATA(buf);

	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	switch (buf_get_type(buf)) {
	case BUF_TYPE_STRING:
		return buf_str_pack(buf);
	}

	/*** If buffer is empty we have nothing to do */

	if (NULL == buf->data) {
		return (BUFT_OK);
	}

	/*** Sanity check: dont' process invalide buffer */

	if (BUFT_OK != buf_is_valid(buf)) {
		DE("Buffer is invalid - can't proceed\n");
		return (-ECANCELED);
	}

	/*** Should we really pack it? */
	if (buf_get_used_count(buf) == buf_get_room_count(buf)) {
		/* No need to pack it */
		return (BUFT_OK);
	}

	/* Here we shrink the buffer */
	new_size = buf_get_used_count(buf);
	if (BUFT_YES == buf_is_canary(buf)) {
		new_size += BUF_T_CANARY_SIZE;
	}

	if (BUFT_OK != buf_resize(buf, new_size)) {
		DE("Can not realloc buf->data\n");
		return (-BUFT_BAD);
	}

	if (BUFT_OK != buf_set_room_count(buf, buf_get_used_count(buf))) {
		DE("Can not set a new room value to the buffer\n");
		return (-BUFT_BAD);
	}

	if (BUFT_YES == buf_is_canary(buf)) {
		if (BUFT_OK != buf_set_canary(buf)) {
			DE("Can not set CANARY to the buffer\n");
			TRY_ABORT();
			return (-BUFT_BAD);
		}
	}

	/* Here we are if buf->used == buf->room */
	BUF_TEST(buf);
	return (BUFT_OK);
}

/* Experimental: Try to set the buf used size automatically */
/* It can be useful if we copied manualy a string into buf_t and we want to update 'used' of the
   buf_t*/
ret_t buf_detect_used(/*@temp@*//*@in@*//*@special@*/buf_t *buf)
/*@uses buf->data@*/
{
	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	/* If the buf is empty - return with error */
	if (0 == buf_get_room_count(buf)) {
		DE("Tryed to detect used in empty buffer?\n");
		TRY_ABORT();
		return (-ECANCELED);
	}

	switch (buf_get_type(buf)) {
	case BUF_TYPE_STRING:
		return buf_str_detect_used(buf);
	default:
		DE("Not supported buf type %d\n", buf_get_type(buf));
	}

	/* We should not get here */
	return (-BUFT_BAD);
}

/* 
 * Extract from buffer field 'number'.
 * 'delims' is set of delimiters, each delimeter 1 character.
 * 'skip' is a pattern to skip after delimeter.
 * 
 * Example:
 * 
 * "Value : red  "
 * 
 * If we want to extract "red": delims=":"
 * skip=" "
 * In this case this funciton finds ":", and skip all " " after it.
 * 
 */

#if 0
buf_t *buf_extract_field(buf_t *buf, const char *delims, const char *skip, size_t number){
	int pos = 0;
	int end = -1;
	int num = 0;
	TESTP(buf, NULL);
	TESTP(delims, NULL);

	/* Find start position */
	while (pos < buf->used) {
		if (buf->data[pos] ) {}
	}
}
#endif

/* Receive from socket; add to the end of the buf; return number of received bytes */
size_t buf_recv(/*@temp@*//*@in@*//*@special@*/buf_t *buf, const int socket, const buf_s64_t expected, const int flags)
/*@uses buf->data@*/
{
	int     rc;
	ssize_t received = -1;

	T_RET_ABORT(buf, -BUFT_NULL_POINTER);

	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	/* Test that we have enough room in the buffer */
	rc = buf_test_room(buf, expected);

	if (BUFT_OK != rc) {
		DE("Can't allocate enough room in buf\n");
		TRY_ABORT();
		return (-ENOMEM);
	}

	received = recv(socket, buf->data + buf_get_used_count(buf), expected, flags);
	if (received > 0) {
		if (BUFT_OK != buf_inc_used(buf, received)) {
			DE("Can not increment 'used'");
			TRY_ABORT();
			return (-EINVAL);
		}
	}

	return (received);
}

buf_t *buf_from_file(const char *filename, buf_t_flags_t buf_type)
{
	int         fd;
	buf_t       *buf;
	struct stat st;
	int         rc;


	T_RET_ABORT(filename, NULL);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		DE("can't open file fir reading: %s\n", filename);
		return (NULL);
	}

	if (0 != fstat(fd, &st)) {
		DE("Can't stat file %s\n", filename);
		rc = close(fd);
		if (0 != rc) {
			DE("Could not close file %s\n", filename);
			TRY_ABORT();
		}
		TRY_ABORT();
		return (NULL);
	}

	//buf = buf_new(st.st_size + 1);
	buf = buf_new(0);
	TESTP(buf, NULL);

	/* If the buf_type is not 0, set buffer type */
	if (0 != buf_type) {
		rc = buf_set_type(buf, buf_type);
		if (BUFT_OK != rc) {
			DE("Could not set type\n");
			TRY_ABORT();
			rc = buf_free(buf);
			return (NULL);
		}
	}

	char *tmp = zmalloc(TMP_BUF_SIZE);
	TESTP(buf, NULL);

	do {
		rc = read(fd, tmp, TMP_BUF_SIZE);

		if (rc < 0) {
			DE("Error on file read: asked %lu, read %d\n", buf_get_room_count(buf), rc);
			rc = close(fd);
			if (0 != rc) {
				DE("Could not close file %s\n", filename);
				TRY_ABORT();
			}

			/* Free the buffer before return with error */
			rc = buf_free(buf);
			if (BUFT_OK != rc) {
				DE("Could not release buf_t\n");
				TRY_ABORT();
			}
			return (NULL);
		}

		const ret_t rc_add = buf_add(buf, tmp, rc);
		if (BUFT_OK != rc_add) {
			DE("Can not add memoty to buf string: error %d | %s\n", rc_add, buf_error_code_to_string(rc_add));
			abort();
		}
	} while (TMP_BUF_SIZE == rc);

	free(tmp);

	rc = close(fd);
	if (0 != rc) {
		DE("Could not close file %s\n", filename);
		TRY_ABORT();
	}
	return (buf);
}

/* TODO: Not tested */
ret_t buf_shrink(buf_t *buf)
{
	TESTP_BUF_DATA(buf);
	buf_s64_t buf_used = buf_get_used_count(buf);

	/* If used size is less than used size, we shring the internal buffer */
	if (buf_used < buf_get_room_count(buf)) {
		int rc = buf_resize(buf, buf_used);
		if (BUFT_OK != rc) {
			DE("Could not resize the buffer data\n");
			TRY_ABORT();
			return rc; /* TODO: Add error code */
		}

		/* Update the room size */
		rc = buf_set_room_count(buf, buf_used);
		if (BUFT_OK != rc) {
			DE("Could not set new used size\n");
			TRY_ABORT();
			return rc; /* TODO: Add error code */
		}
	}

	return BUFT_OK;
}

int buf_to_file(buf_t *buf, buf_t *file, mode_t mode)
{
	int fd;
	int rc = -BUFT_BAD;

	TESTP_BUF_DATA(buf);
	TESTP_BUF_DATA(file);

	rc = buf_is_change_allowed(buf);

	if (BUFT_OK != rc) {
		DE("Buffer manipulation is not allowed, the buffer is immutable or locked\n");
		TRY_ABORT();
		return rc;
	}

	fd = open(file->data, O_WRONLY);
	if (fd < 0) {
		DE("can't open file fir reading: %s\n", file->data);
		return (-BUFT_BAD);
	}

	rc = write(fd, buf->data, buf_get_room_count(buf));

	if (rc < 0 || (buf_s64_t)rc != buf_get_room_count(buf)) {
		DE("Error on file writinf: asked %lu, read %d\n", buf_get_room_count(buf), rc);
		rc = -BUFT_BAD;
	} else rc = BUFT_OK;

	if (mode != 0) {
		if (0 != fchmod(fd, mode)) {
			DE("Can't chmod file %s\n", file->data);
		}
	}

	rc = close(fd);
	if (0 != rc) {
		DE("Could not close file %s\n", file->data);
		TRY_ABORT();
		rc = -BUFT_FILE_CLOSE;
	}
	return (rc);
}


//buf_t *buf_transform(buf_t *buf, )
