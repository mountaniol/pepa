#ifndef _BUF_T_H_
#define _BUF_T_H_

#include <stddef.h>
#include <sys/types.h>
#include "se_debug.h"
#include "se_tests.h"
#define BUF_NOISY

/* For uint32_t / uint8_t */
//#include <linux/types.h>
/*@-skipposixheaders@*/
#include <stdint.h>
/*@=skipposixheaders@*/
/* For err_t */
//#include "mp-common.h"

/* This is a switchable version; depending on the global abort flag it will abort or rturn an error */
#define ABORT_ON_ERROR_OFF (0)
#define ABORT_ON_ERROR_ON (1)


/****************************** TYPES ******************************/

typedef uint32_t ret_t;

/* Whidth of the buf type */
typedef uint8_t buf_t_type_t;

/* Whidth of the flags field */
typedef uint8_t buf_t_flags_t;

#define BUFT_CHAR_ERR (0xFF)

typedef int64_t buf_s64_t;
typedef int32_t buf_s32_t;
typedef uint32_t buf_circ_usize_t;


/* This is just a regular buffer, keeping user's raw data.
   User knows what to do with it, we don't care */
#define BUF_TYPE_RAW        	0

/* String buffer. In this case, additional tests enabled */
#define BUF_TYPE_STRING        (1)

/* Bit buffer */
#define BUF_TYPE_BIT      		(2)

/* Array of elements */
#define BUF_TYPE_ARR      		(3)

#define BUF_TYPE_CIRC			(4)


/* Circular Buffer properties */
#define BUF_T_CIRC_HEAD_WIDTH (32)
#define BUF_T_CIRC_MASK (0x0000FFFF)

/** Flags **/

/* Buffer is immutable; it means, it created from data,
	and can not be changed any way in the future.
	The constant buffer can not have canary.
	It is a special case of buffer.
	Probably, it should be a type, not a flag */
#define BUF_FLAG_IMMUTABLE     (1 << 0)

/* Buffer is compressed */
#define BUF_FLAG_COMPRESSED (1 << 1)

/* Buffer is enctypted */
#define BUF_FLAG_ENCRYPTED  (1 << 2)

/* Buffer is enctypted */
#define BUF_FLAG_CANARY  (1 << 3)

/* Buffer is crc32 protected */
#define BUF_FLAG_CRC  (1 << 4)

/* Buffer can not change its size:
   we might need it for implmenet a circular buffer ot top of buf ARRAY */
#define BUF_FLAG_FIXED_SIZE  (1 << 5)

/* Buffer is locked, not data manipulation is allowed.
   The buffer can be any type, and it can be locked and ulocked dynamically */
#define BUF_FLAG_LOCKED  (1 << 6)

/* Size of canary */
//typedef uint32_t buf_t_canary_t;
/* We use 2 characters as canary tail = 1 short */
//typedef uint16_t buf_t_canary_t;
typedef uint8_t buf_t_canary_t;
typedef uint8_t buf_t_checksum_t;

/* This function documented later in this header */
extern int bug_get_abort_flag(void);

/****************************** STRUCTS ******************************/

typedef struct array {
	buf_s32_t size;
	buf_s32_t members;
} arr_used_t;

typedef struct head_tail_struct {
	buf_circ_usize_t head;
	buf_circ_usize_t tail;
} head_tail_t;

/* Simple struct to hold a buffer / string and its size / lenght */

#ifdef BUF_DEBUG
struct buf_t_struct {
	buf_s64_t room;           /* Overall allocated size */
	/* The next union is shows how many of the 'room' is used;
	   THe used size can be less than allocated, i.e., 'used' <= 'used' */
	union {
		buf_s64_t used;           	/* For string and raw buf: used size */
		head_tail_t ht;             /* For cirrcular buffer: head and tail of the circular buffer */
		arr_used_t arr;				/* For array: how many members in arr and a member size */
	};
	buf_t_flags_t flags;        /* Buffer flags. Optional. We may use it as we wish. */
	/*@temp@*/char *data;       /* Pointer to data */

	/* Where this buffer allocated: function */
	const char *func;
	/* Where this buffer allocated: file */
	const char *filename;
	/* Where this buffer allocated: line */
	int line;
};

#else /* Not debug */
/* Simple struct to hold a buffer / string and its size / lenght */
struct buf_t_struct {
	buf_s64_t room;           /* Allocated size */
	union {
		buf_s64_t used;           	/* For string and raw buf: used size */
		head_tail_t ht;             /* For cirrcular buffer: head and tail of the circular buffer */
		arr_used_t arr;				/* For array: how many members in arr and a member size */
	};
	buf_t_type_t type;        /* Buffer type. Optional. We may use it as we wish. */
	buf_t_flags_t flags;        /* Buffer flags. Optional. We may use it as we wish. */
	/*@only@*/ char *data;       /* Pointer to data */
};
#endif

/****************************** ERRORS ******************************/

typedef enum {
	BUFT_NO = 1,    /* NO in case a function returns YES or NO */
	BUFT_OK = 0,    /* Success status */
	BUFT_YES = 0,   /* YES in case a function returns YES or NO */
	BUFT_ERROR_BASE = 10000,
	BUFT_BAD,  /* Error status */
	BUFT_NULL_POINTER,  /* Got a new pointer as an argument */
	BUFT_NULL_DATA,  /* The buffer data is NULL, not expected it */
	BUFT_BAD_BUFT_TYPE,  /* The type of buf_t is unknown; it it memory damage or version mismatch */
	BUFT_BAD_CANARY,  /* The CANARY sufix is damaged */
	BUFT_NO_CANARY,  /* Buffers does not have CANARY flag set, but is was expected */
	BUFT_WRONG_BUF_FLAG,  /* Error status */
	BUFT_CANNOT_SET_CANARY,  /* Error status */
	BUFT_OUT_OF_LIMIT_OP,  /* The operation can not be executed because out of limit situation detected */
	BUFT_TOO_SMALL,  /* The operation can not be executed because there is not space to add */
	BUFT_ALLOCATE,  /* For some reason could not allocate memory */
	BUFT_INCREMENT,  /* For some reason could decrement room */
	BUFT_DECREMENT,  /* For some reason could decrement room */
	BUFT_BAD_USED,  /* For some reason could not allocate memory */
	BUFT_BAD_ROOM,  /* Room size is damaged, returned from validation function */
	BUFT_BAD_SIZE,  /* A bad size passed in argument */
	BUFT_SET_ROOM_SIZE,  /* Can not set new ->room size by some reason */
	BUFT_SET_USED_SIZE,  /* Can not set new ->used size by some reason */
	BUFT_IMMUTABLE_DAMAGED,  /* The buffer is constant, but looks like the buffer is invalid */
	BUFT_IS_IMMUTABLE,  /* Caller asked for an operation forbidden for a constant buffer  */
	BUFT_FLAG_UNSET,  /* Can not unset a flag  */
	BUFT_IS_LOCKED,  /* Caller asked for an operation forbidden for a locked buffer  */
	BUFT_UNKNOWN_TYPE,  /* Unknown buffer type; it it a severe problem indicating version mismatcj or memory corruption */
	BUFT_HAS_CANARY,  /* The buffer is asked to set data, but the buffer has canary. Before set new ->data, the CANARY flag should be removed */
	BUFT_FILE_CLOSE,  /* Cannot close opened file */
	BUFT_STRING_NO_NULL,  /* Cannot close opened file */
} buf_t_error_t;

/**
 * @author Sebastian Mountaniol (12/2/23)
 * @brief Print string representation of an error
 * @param buf_t_error_t er    Error code, type buf_t_error_t
 * @return char* A string describing the error
 * @details 
 */
extern char *buf_error_code_to_string(int er);

typedef /*@abstract@*/ struct buf_t_struct buf_t;

/**
 * @define TRY_ABORT
 * @brief If there is 'abort on error' is set, this macro stops
 *  execution and generates core file
 */
#define TRY_ABORT() do{ if(ABORT_ON_ERROR_ON == bug_get_abort_flag()) {DE("Abort in %s +%d\n", __FILE__, __LINE__);abort();} } while(0)

/**
 * @define T_RET_ABORT
 * @brief This macro wither abort if abort flag is set ON, or return with error, if no abort flag set.
 * @details See bug_get_abort_flag() for more details
 */
#define T_RET_ABORT(x, ret) do {if (NULL == x) {TRY_ABORT(); DE("[[ ERROR! ]]: Pointer %s is NULL\n", #x); return ret;}} while(0)
								

/**
 * @define TESTP_BUF_DATA
 * @brief This macro test both the buffer, and the buffer->data.
 *  	  In case one of these is NULL, it return from function
 *  	  with error or, if abort flag set to ON (see
 *  	  bug_get_abort_flag()) it aborts execution with core
 *  	  file.
 */
#define TESTP_BUF_DATA(_Buf) do {T_RET_ABORT(_Buf, -BUFT_NULL_POINTER); T_RET_ABORT(_Buf->data, -BUFT_NULL_DATA); } while(0)
//#define T_RET_ABORT(x, ret) do {if(NULL == x) {DE("[[ ASSERT! ]] %s == NULL\n", #x);abort(); }} while(0)

/* CANARY: Set a mark after allocated buffer*/
/* PRO and CONTRA of this method:*/
/* PRO: It can help to catch memory problems */
/* Contras: The buffer increased, and buffer validation should be run on every buffer operation */
/* The mark we set at the end of the buf if PROTECTED flag is enabled */

/* Canary size: the size of special buffer added after data to detect data tail corruption */
#define BUF_T_CANARY_SIZE (sizeof(buf_t_canary_t))

//#define BUF_T_CANARY_WORD ((buf_t_canary_t) 0xFEE1F4EE)
#define BUF_T_CANARY_WORD ((buf_t_canary_t) 0x31415926)

// The CANARY char pattern is : 10101010 = 0XAA
#define BUF_T_CANARY_CHAR_PATTERN 0XAA
#define BUF_T_CANARY_SHORT_PATTERN 0XAAAA

//0x12345678)

#define BUF_T_CRC_SIZE (sizeof(buf_t_checksum_t))

/* Size of a regular buf_t structure */
#define BUF_T_STRUCT_SIZE (sizeof(buf_t))

/* Size of buf_t structure for network transmittion: without the last 'char *data' pointer */
#define BUF_T_STRUCT_NET_SIZE (sizeof(buf_t) - sizeof(char*))

/* How much bytes will be transmitted to send buf_t + its actual data */
#define BUF_T_NET_SEND_SIZE(b) (BUF_T_STRUCT_NET_SIZE + b->used)


/* Found on Internet: detect max and min possible values for any data type.
   https://copyprogramming.com/howto/min-and-max-value-of-data-type-in-c */
#define MAX_OF(type) \
    (((type)(~0LLU) > (type)((1LLU<<((sizeof(type)<<3)-1))-1LLU)) ? (long long unsigned int)(type)(~0LLU) : (long long unsigned int)(type)((1LLU<<((sizeof(type)<<3)-1))-1LLU))
#define MIN_OF(type) \
    (((type)(1LLU<<((sizeof(type)<<3)-1)) < (type)1) ? (long long int)((~0LLU)-((1LLU<<((sizeof(type)<<3)-1))-1LLU)) : 0LL)



/****************************** FUNCTION: buf_t.c ******************************/

/*
 * @author Sebastian Mountaniol (11/20/23)
 * @brief This function returns abort flag. If abort flag is set
 *  	  to ON, the execution will stopped in a function where
 *  	  the error occurs, and a core file will be generated.
 * @return int ABORT_ON_ERROR_ON if the abort flag is set,
 *  	   ABORT_ON_ERROR_OFF if the abort flag is not set
 * @details Abort flag is set / unset with functions
 *  		buf_set_abort_flag(), buf_unset_abort_flag()
 */
 __attribute__((warn_unused_result))
extern int bug_get_abort_flag(void);

/**
 * @define T_RET_ABORT
 * @brief This macro either abort execution or return an error
 *  	  'ret'
 * @details This macro depends on the abort flag, returned from
 *  		function bug_get_abort_flag(). If the abort flag is
 *  		set to ABORT_ON_ERROR_ON, it aborts execution;
 *  		otherwise, if the flag is ABORT_ON_ERROR_OFF,
 *  		it returns the error 'ret.' This macro is useful for
 *  		debug when we want to trace where the problem
 *  		starts.
 */

/**
 * @brief set "abort on errors" state
 * @param void
 */
extern void buf_set_abort_flag(void);

/**
 * @brief Unset "abort on errors" state
 */
extern void buf_unset_abort_flag(void);

/**
 * @brief Set default buf_t flags. Will be applied for every allocated new buf_t struct.
 * @param buf_t_flags_t f
 */
extern void buf_default_flags(buf_t_flags_t f);

/**
 * @brief Set new data into buffer. The buf_t *buf must be clean, i.e. buf->data == NULL and
 *  buf->room == 0; After the new buffer 'data' set, the buf->len also set to 'len'
 * @param buf_t * buf 'buf_t' buffer to set new data 'data'
 * @param char * data Data to set into the buf_t
 * @param size_t size Size of the new 'data'
 * @param size_t len Length of data in the buffer, user must provide it
 * @return err_t Returns BUFT_OK on success
 *  Return -EACCESS if the buffer is read-only
 *  Return -EINVAL if buffer or data is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_data(/*@temp@*//*@in@*//*@special@*/buf_t *buf, /*@null@*/ /*@only@*/ /*@in@*/char *data, const buf_s64_t size, const buf_s64_t len);

/**
 * @brief Returns buf->data
 * @param buf - Buf to return data from
 * @return void* Returns buf->data; NULL on an error
 * @details 
 */
extern void /*@temp@*//*@null@*/ *buf_get_data_ptr(/*@temp@*//*@in@*//*@special@*/buf_t *buf);


/**
 * @brief Test that the buf->data is NULL
 * @param buf - Buffer to test
 * @return ret_t YES if data is NULL, NO if data not NULL,
 *  	   -EINVAL if the buffer is NULL pointer
 */
__attribute__((warn_unused_result))
extern ret_t buf_data_is_null(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Test bufer validity
 * @param buf_t * buf Buffer to test
 * @return err_t Returns BUFT_OK if the buffer is valid.
 * 	Returns EINVAL if the 'buf' == NULL.
 * 	Returns EBAD if this buffer is invalid.
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_valid(/*@temp@*/ /*@in@*/buf_t *buf);

/**
 * @brief Allocate buf_t. A new buffer of 'size' will be
 *    allocated.
 * @author se (03/04/2020)
 * @param size_t size Data buffer size, may be 0
 * @return buf_t* New buf_t structure.
 */
__attribute__((warn_unused_result))
extern /*@null@*/ /*@in@*/buf_t *buf_new(buf_s64_t size);

/**
 * @brief Set a data into buffer and lock the buffer, i.e. turn the buf into "read-only".
 * @param buf_t * buf Buffer to set read-only
 * @param char * data Data the buf_t will contain. It is legal if this parameter == NULL
 * @param size_t size Size of the buffer. If data == NULL this argument must be 0
 * @return err_t BUFT_OK on success
 * 	Returns -ECANCELED if data == NULL but size > 0
 * 	Returns -EACCESS if this buffer already marked as read-only.
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_data_immutable(/*@temp@*//*@in@*//*@special@*/buf_t *buf, /*@null@*//*@only@*//*@in@*/char *data, buf_s64_t size);

/**
 * @brief 'Steal' data from buffer. After this operation the internal buffer 'data' returned to
 *    caller. After this function buf->data set to NULL, buf->len = 0, buf->size = 0
 * @param buf_t * buf Buffer to extract data buffer
 * @return void* Data buffer pointer on success, NULL on error. Warning: if the but_t did not have a
 * 	buffer (i.e. buf->data was NULL) the NULL will be returned.
 */
__attribute__((warn_unused_result))
extern /*@null@*//*@only@*/void *buf_steal_data(/*@temp@*/ /*@in@*//*@special@*/ buf_t *buf);

/**
 * @brief Return data buffer from the buffer and release the buffer. After this operation the buf_t
 *    structure will be completly destroyed. WARNING: disregarding to the return value the buf_t
 *    will be destoyed!
 * @param buf_t * buf Buffer to extract data
 * @return void* Pointer to buffer on success (buf if the buffer is empty NULL will be returned),
 */
__attribute__((warn_unused_result))
extern /*@null@*/void *buf_to_data(/*@only@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Remove data from buffer (and free the data),
 *  	  set buf->room = buf->len = 0, remove all flags
 * @param buf Buffer to remove data from
 * @return err_t EOK if all right
 * 	EINVAL if buf is NULL pointer
 * 	EACCESS if the buffer is read-only, buffer kept untouched
 * @details If the buffer is invalid (see buf_is_valid()),
 * @details the opreration won't be interrupted and buffer will be cleaned.
 */
__attribute__((warn_unused_result))
extern ret_t buf_reset(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Remove data from buffer (and free the data),
 *  	  clean counters buf keep flags
 * @param buf Buffer to remove data from
 * @return err_t EOK if all right
 * 	EINVAL if buf is NULL pointer
 * 	EACCESS if the buffer is read-only, buffer kept untouched
 * @details If the buffer is invalid (see buf_is_valid()),
 * @details the opreration won't be interrupted and buffer will be cleaned.
 */
__attribute__((warn_unused_result))
extern ret_t buf_clean(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Allocate additional 'size' in the tail of buf_t data
 *    buffer; existing content kept unchanged. The new
 *    memory will be cleaned. The 'size' argument must be >
 *    0. For removing buf->data use 'buf_free_force()'
 * @author se (06/04/2020)
 * @param buf_t * buf Buffer to grow
 * @param size_t size How many byte to add
 * @return int BUFT_OK on success
 * 	-EINVAL if buf == NULL
 * 	-EACCESS if buf is read-only
 *  -ENOMEM if allocation of additional space failed. In this
 *  case the buffer kept untouched. -ENOKEY if the buffer marked
 *  as CAANRY but CANARY work can't be added.
 */
__attribute__((warn_unused_result))
extern ret_t buf_add_room(/*@temp@*//*@in@*//*@special@*/buf_t *buf, buf_s64_t size);

/**
 * @brief The function accept size in bytes that caller wants to
 *    add into buf. It check if additional room needed. If
 *    yes, calls buf_room() to increase room. The 'expect'
 *    ca be == 0
 * @author se (06/04/2020)
 * @param buf_t * buf Buffer to test
 * @param size_t expect How many bytes will be added
 * @return int BUFT_OK if the buffer has sufficient room or if room added succesfully
 * 	EINVAL if buf is NULL or 'expected' == 0
 * 	Also can return all error statuses of buf_add_room()
 */
__attribute__((warn_unused_result))
extern ret_t buf_test_room(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t expect);

/**
 * @brief Feel internal buffer with zeros
 * @param buf_t* buf   Buffer to fill with zeros
 * @return ret_t BUFT_OK on success, else a negative error code
 */
__attribute__((warn_unused_result))
extern ret_t buf_fill_with_zeros(/*@temp@*//*@in@*//*@special@*/buf_t *buf);
/**
 * @brief Free buf; if buf->data is not empty, free buf->data
 * @param buf_t * buf Buffer to remove
 * @return int BUFT_OK on success
 * 	EINVAL is the buf is NULL pointer
 * 	EACCESS if the buf is read-only
 * 	ECANCELED if the buffer is invalid
 */
__attribute__((warn_unused_result))
extern ret_t buf_free(/*@only@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Append (copy) buffer "new_data" of size "size" to the tail of buf_t->data
 *    New memory allocated if needed.
 * @param buf_t * buf Buffer to append to buf->data
 * @param const char* new_data This buffer will be appended (copied) to the tail of buf->data
 * @param const size_t size Size of 'new_data' in bytes
 * @return int BUFT_OK on success
 * 	EINVAL if: 'buf' == NULL, or 'new_data' == NULL, or 'size' == 0
 * 	EACCESS if the 'buf' is read-only
 * 	ENOMEM if new memory can't be allocated
 */
__attribute__((warn_unused_result))
extern ret_t buf_add(/*@temp@*//*@in@*//*@special@*/buf_t *buf, /*@temp@*//*@in@*/const char *new_data, const buf_s64_t size);

/**
 * @brief Return size in bytes of used memory (which is buf->used)
 * @param buf_t * buf Buffer to check
 * @return ssize_t Number of bytes used on success
 * 	EINVAL if the 'buf' == NULL
 */
__attribute__((warn_unused_result))
extern buf_s64_t buf_get_used_count(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Set a new value of the buf->used
 * @param buf - Buffer to set the new value
 * @param used - The new value to set
 * @return ret_t 
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_used(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t used);

/**
 * @brief Increment the buf->used value by 'inc'
 * @param buf - The buffer to increment the value of the
 *  		  buf->used
 * @param inc - The value to add to the buf->used
 * 
 * @return ret_t BUFT_OK on success, BAD on an error
 * @details For now, this operation not tested for out of limit
 *  		situation. So if ->used value it too high the
 *  		operation will be still executed 
 */
__attribute__((warn_unused_result))
extern ret_t buf_inc_used(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t used);

/**
 * @brief Decrement value of the buf->used 
 * @param buf - Buffer to decrement the '->used'
 * @param dec - Decrement ->used by this value; the 'used' must
 *  		   be > 0 (it can not be < 0, however)
 * @return ret_t BUFT_OK on success, BAD on an error
 */
__attribute__((warn_unused_result))
extern ret_t buf_dec_used(/*@temp@*//*@in@*/buf_t *buf, buf_s64_t dec);

/**
 * @brief Test whether the buffer is immutable
 * @param buf_t* buf   Buffer to test
 * @return ret_t Return BUFT_YES is the buffer is immutable,
 *  	   BUFT_NO if the buffer is not immutable
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_immutable(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Test whether the buffer is compressed
 * @param buf_t* buf   Buffer to test
 * @return ret_t Return BUFT_YES is the buffer is compressed,
 *  	   BUFT_NO if the buffer is not compressed
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_compressed(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Test whether the buffer is encrypted
 * @param buf_t* buf   Buffer to test
 * @return ret_t Return BUFT_YES is the buffer is encrypted,
 *  	   BUFT_NO if the buffer is not encrypted
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_encrypted(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Test whether the buffer has canary mark 
 * @param buf_t* buf   Buffer to test
 * @return ret_t Return BUFT_YES is the buffer has the canary
 *  	   mark, BUFT_NO if the buffer doesn't have the canary
 *  	   mark
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_canary(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Test whether the buffer is checksum protected
 * @param buf_t* buf   Buffer to test
 * @return ret_t Return BUFT_YES is the buffer is checksum
 *  	   protected, BUFT_NO if the buffer is not checksum
 *  	   protected
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_crc(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Test whether the buffer has fixed size
 * @param buf_t* buf   Buffer to test
 * @return ret_t Return BUFT_YES is the buffer is a fixed size
 *  	   buffer, BUFT_NO if it is not a fixed size
 *  	   buffer
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_fixed(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Test whether the buffer is locked
 * @param buf_t* buf   Buffer to test
 * @return ret_t Return BUFT_YES is the buffer is locked,
 *  	   BUFT_NO if the buffer is not locked
 * @details A locked buffer is immutable until it unlocked
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_locked(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Return size of memory currently allocated for this 'buf' (which is buf->room)
 * @param buf_t * buf Buffer to test
 * @return ssize_t How many bytes allocated for this 'buf'
 * 	EINVAL if the 'buf' == NULL
 */
__attribute__((warn_unused_result))
extern buf_s64_t buf_get_room_count(/*@temp@*/ /*@in@*/buf_t *buf);

/**
 * @brief Set new buf->room value
 * @param buf - Buffer to set the a value
 * @param room - The new value of buf->room to set
 * @return ret_t BUFT_OK on success, BAD on an error
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_room_count(/*@temp@*/ /*@in@*/ buf_t *buf, buf_s64_t room);

/**
 * @brief Increment value of buf->room by 'inc' value
 * @param buf - Buffer to increment the buf->room value
 * @param inc - The value to add to buf->room
 * @return ret_t BUFT_OK on sucess, BAD on an error
 */
__attribute__((warn_unused_result))
extern ret_t buf_inc_room_count(/*@temp@*/ /*@in@*/ buf_t *buf, buf_s64_t inc);

/**
 * @brief Decrement the value of buf->room by 'dec' value
 * @param buf - The buffer to decrement buf->room
 * @param dec - The value to substract from nuf->room
 * 
 * @return ret_t BUFT_OK on sucess, BAD on an error
 * @details The 'dec' must be less or equal to the buf->room,
 *  		else BAD error returned and no value decremented
 */
__attribute__((warn_unused_result))
extern ret_t buf_dec_room_count(/*@temp@*/ /*@in@*/buf_t *buf, buf_s64_t dec);

/**
 * @brief Shrink buf->data to buf->len.
 * @brief This function may be used when you finished with the buf_t and
 *    its size won't change anymore.
 *    The unused memory will be released.
 * @param buf_t * buf Buffer to pack
 * @return err_t BUFT_OK on success;
 * 	BUFT_OK if this buffer is empty (buf->data == NULL) BUFT_OK returned
 * 	BUFT_OK if this buffer should not be packed (buf->used == buf->room)
 * 	EINVAL id 'buf' == NULL
 * 	ECANCELED if this buffer is invalid (see buf_is_valid)
 * 	ENOMEM if internal realloc can't reallocate / shring memory
 * 	Also can return one of buf_set_canary() errors
 */
__attribute__((warn_unused_result))
extern ret_t buf_pack(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Return current value of gloabs buf_t flags (the flags added to every new allocate buf_t)
 * @return buf_t_flags_t - value of global buf_t flags
 */
__attribute__((warn_unused_result))
extern buf_t_flags_t buf_save_flags(void);

/**
 * @brief Restore global buf_t flags
 * @param buf_t_flags_t flags - flags to set
 */
extern void buf_restore_flags(buf_t_flags_t flags);

/**** Mark / Unmark flags */

/**
 * @brief Mark (set flag) the buf as a buffer containing string buffer
 * data
 * @param buf_t * buf Buffer to mark
 * @return err_t BUFT_OK on success, -EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_type_string(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Mark (set flag) the buf as a buffer containing an
 * array
 * @param buf_t * buf Buffer to mark as array
 * @return err_t BUFT_OK on success, -EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_type_array(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Set the buffer flags 
 * @param buf - Buffer to set flags
 * @param f - Flag(s) to set
 * @return ret_t BUFT_OK on success, -EINVAL if NULL pointer
 *  	   received
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_flag(/*@temp@*//*@in@*/buf_t *buf, buf_t_flags_t f);

/**
 * @brief Unset a flag (or flags) from the buffer flags
 * @param buf - Buffer to unset flag(s)
 * @param f - Flag(s) to unset
 * @return ret_t 
 */
__attribute__((warn_unused_result))
extern ret_t buf_rm_flag(/*@temp@*//*@in@*/buf_t *buf, buf_t_flags_t f);

/**
 * @brief Set type of buffer
 * @param buf_t* buf   Buffer to set type
 * @param buf_t_type_t t     Buffer type
 * @return ret_t BUFT_OK on success. A negative error code on
 *  	   failure.
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_type(/*@temp@*//*@in@*/buf_t *buf, buf_t_type_t t);

/**
 * @brief Get type of the buffer
 * @param buf_t* buf   Buffer to read type from
 * @return buf_t_flags_t Type of the biffer; WARNING:
 *  	   On an error it returns BUFT_CHAR_ERR! Test it for
 *  	   this error.
 */
__attribute__((warn_unused_result))
extern buf_t_flags_t buf_get_type(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Mark (set flag) the buf as a buffer containing read-only data
 * @param buf_t * buf Buffer to mark
 * @return err_t BUFT_OK on success, -EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_mark_immutable(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Mark (set flag) the buf as a buffer containing compressed data
 * @param buf_t * buf Buffer to mark
 * @return err_t BUFT_OK on success, EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_mark_compresed(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Mark (set flag) the buf as a buffer containing encrypted data
 * @param buf_t * buf Buffer to mark
 * @return err_t BUFT_OK on success, EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_mark_encrypted(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Mark (set flag) that the buf has canary word in the end of the buffer
 * @param buf_t * buf Buffer to mark
 * @return err_t BUFT_OK on success, EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_mark_canary(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Mark (set flag) that the buf has CRC word in the end of the buffer
 * @param buf_t * buf Buffer to mark
 * @return err_t BUFT_OK on success, EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_mark_crc(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Mark (set flag) that the buf is fixed size
 * @param buf_t * buf Buffer to mark
 * @return err_t BUFT_OK on success, a negative error code on
 *  	   abort
 */
__attribute__((warn_unused_result))
extern ret_t buf_mark_fixed(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Test whether the buffer can not be changed or not. The
 *  	  buffer can not be changed if it IMMUTABLE or LOCKED
 * @param buf_t* buf   Buffer to test
 * @return ret_t BUFT_YES if the buffer can be changed,
 *  	   -BUFT_IS_IMMUTABLE if the buffer is immutable,
 *  		-BUFT_IS_LOCKED if the buffer is locked
 */
__attribute__((warn_unused_result))
extern ret_t buf_is_change_allowed(buf_t *buf);

/**
 * @brief Remove "read-only" mark (unset flag) from the buf
 * @param buf_t * buf Buffer to unmark
 * @return BUFT_OK on success, an error code on failure
 */
__attribute__((warn_unused_result))
extern ret_t buf_unmark_immutable(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Unlock locked buffer
 * @param buf_t * buf Buffer to unmark
 * @return BUFT_OK on success, an error code on failure* @details 
 */
__attribute__((warn_unused_result))
extern ret_t buf_unmark_locked(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Unlock locked buffer; alias for buf_unmark_locked()
 * @param buf_t * buf Buffer to unmark
 * @return BUFT_OK on success, an error code on failure*
 */
__attribute__((warn_unused_result))
extern ret_t buf_unlock(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Lock the buffer; no operations, include buf_free are
 *  	  allowed until it locked
 * @param buf_t* buf   Buffer to lock
 * @return ret_t BUFT_OK on succedd, an error code on failure
 */
__attribute__((warn_unused_result))
extern ret_t buf_mark_locked(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief An alias for buf_unmark_locked(): lock a buffer, no operations, include buf_free are
 *  	  allowed until it locked
 * @param buf_t* buf   Buffer to lock
 * @return ret_t BUFT_OK on success, an error code on failure
 */
__attribute__((warn_unused_result))
extern ret_t buf_lock(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Remove "compressed" mark (unset flag) from the buf
 * @param buf_t * buf Buffer to unmark
 * @return err_t BUFT_OK on success, EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_unmark_compressed(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Remove "encryptes" mark (unset flag) from the buf
 * @param buf_t * buf Buffer to unmark
 * @return err_t BUFT_OK on success, EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_unmark_encrypted(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Remove "canary" mark (unset flag) from the buf
 * @param buf_t * buf Buffer to unmark
 * @return err_t BUFT_OK on success, EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_unmark_canary(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Remove "crc" mark (unset flag) from the buf
 * @param buf_t * buf Buffer to unmark
 * @return err_t BUFT_OK on success, EINVAL if buf is NULL
 */
__attribute__((warn_unused_result))
extern ret_t buf_unmark_crc(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Remove "fixed_size" mark (unset flag) from the buf
 * @param buf_t * buf Buffer to unmark
 * @return err_t BUFT_OK on success, a negative error code on
 *  	   failure
 */
__attribute__((warn_unused_result))
extern ret_t buf_unmark_fixed(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Set the CANARY mark in the end of the buffer. The
 *  	  buf_t must be marked as CANARY, else an error
 *  	  returned.
 * @param buf_t * buf Buffer to set CANARy pattern
 * @return err_t BUFT_OK on success,
 * 	EINVAL if buf is NULL,
 *  ECANCELED if the buf does not have CANARY or if the CANARY
 *  pattern is not valid right after it set
 * @details If the buf doesn't have CANARY flag it will return ECANCELED.
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_canary(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Set CAANRY mark in the end of the buffer and apply ANARY flag on the buffer
 * @param buf_t * buf Buffer to set CANARY
 * @return err_t BUFT_OK on success,
 * 	-EINVAL if buf is NULL,
 * 	-ECANCELED if the buffer is too small to set CANARY mark,
 * 	or one of the but_set_canary() function errors
 * @details Pay attention, the buffer will be decreased by 1 byte, and the last byte of the buffer
 *    will be replaced with CANARY mark. You must reserve the last byte for it.
 */
__attribute__((warn_unused_result))
extern ret_t buf_force_canary(/*@temp@*//*@in@*/ buf_t *buf);

/**
 * @brief Check that CANARY mark is untouched
 * @param buf_t * buf Buffer to check
 * @return err_t BUFT_OK if CANARY word is untouched,
 * 	-EINVAL if the buf is NULL,
 * 	-ECANCELED if buf is not marked as a CANARY buffer,
 * 	EBAD if canary mark is invalid
 */
__attribute__((warn_unused_result))
extern ret_t buf_test_canary(/*@temp@*//*@in@*//*@special@*/ buf_t *buf);

/**
 * @brief Return current value of CANARY byte
 * @param buf_t * buf Buffer to read CANARY
 * @return buf_t_canary_t Value of CANARY byte, ((buf_t_canary_t)-1) on error
 * @details You may want to use this function if CANARY is damaged
 */
__attribute__((warn_unused_result))
extern buf_t_canary_t buf_get_canary(/*@temp@*//*@in@*//*@special@*/ buf_t *buf);

/**
 * @brief Print flags of the buffer. Useful for debug
 * @param buf_t * buf Buf to read flags
 */
extern void buf_print_flags(/*@temp@*//*@in@*/ buf_t *buf);


/**
 * @brief Test wjether this buffer is a raw buffer
 * @param buf_t* buf   Buffer to test
 * @return int BUFT_YES if the buffer is a raw buffer,
 *  	   BUFT_NO if the buffer is not a raw buffer
 * @details A raw buffer is a buffer with no type set.
 */
__attribute__((warn_unused_result))
extern int buf_type_is_raw(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Test if the buffer is a string buffer
 * @param buf_t * buf Buffer to check
 * @return int BUFT_YES if the buf a string buffer
 * 	Returns -EINVAL if buf is NULL
 * 	Returns 1 if not a string buffer
 */
__attribute__((warn_unused_result))
extern int buf_type_is_string(/*@temp@*/ /*@in@*/buf_t *buf);

/**
 * @brief Test if the buffer is an array buffer
 * @param buf_t * buf Buffer to check
 * @return int BUFT_YES if the buf an array buffer
 * 	Returns -EINVAL if buf is NULL
 * 	Returns 1 if not an array buffer
 */
__attribute__((warn_unused_result))
extern int buf_type_is_array(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Test if the buffer is a bit buffer
 * @param buf - Buffer to test
 * @return int BUFT_OK if it is a bit buffer, -EINVAL if the
 *  	   buffer is NULL, 1 if the buffer is not a bit buffer
 */
__attribute__((warn_unused_result))
extern int buf_type_is_bit(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief Test if the buffer is a circular buffer
 * @param buf - Buffer to test
 * @return int - BUFT_OK  if the buffer is a circular buffer,
 *  	   1 if not, -EINVAL if the buffer id NULL pointer
 */
__attribute__((warn_unused_result))
extern int buf_type_is_circ(/*@temp@*/ /*@in@*/buf_t *buf);
/**
 * @brief If you played with the buffer's data (for example, copied / replaced tezt in the
 *    buf->data) this function will help to detect right buf->used value.
 * @param buf_t * buf Buffer to analyze
 * @return err_t BUFT_OK on succes + buf->used set to a new value
 * 	EINVAL is 'buf' is NULL
 * 	ECANCELED if the buf is invalid or if the buf is empty
 */
__attribute__((warn_unused_result))
extern ret_t buf_detect_used(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/**
 * @brief Receive from socket into buffer
 * @param buf_t * buf Buffer to save received data
 * @param const int socket Opened socket
 * @param const size_t expected How many bytes expected
 * @param const int flags Flags to pass to recv() function
 * @return ssize_t Number of received bytes
 * 	EINVAL if buf is NULL, else returns status of recv() function
 */
__attribute__((warn_unused_result))
extern size_t buf_recv(/*@temp@*/ /*@in@*/ /*@special@*/buf_t *buf, const int socket, const buf_s64_t expected, const int flags);

/**
 * @brief Load content of the file into buf_t
 * @param const char* filename Full name of file to load into
 *  			buf_t
 * @param buf_t_flags_t buf_type _type of buffer to create
 * @return buf_t* Buffer containin the contentof the file
 * @details The type of buf_t is not set. The user should decide
 *  		either they want to set it or not.
 */
__attribute__((warn_unused_result))
extern buf_t *buf_from_file(const char *filename, buf_t_flags_t buf_type);

/**
 * @brief Save content of the buffer to the givel file
 * @param buf_t* buf   Buffer to save
 * @param buf_t* file  Buffer containing name of the file to
 *  		   save tje buffer
 * @param mode_t mode  Mode of the file; after the file created
 *  			 and content of the buf_t is saved,
 *  			 this function will change the file permission
 *  			 accordingly to the given 'mode'
 * @return int BUFT_OK on success, a negative value on an error
 * @details See 'man fchmod' for mode_t format
 */
__attribute__((warn_unused_result))
extern int buf_to_file(buf_t *buf, buf_t *file, mode_t mode);

/**
 * @brief Shrink buffer: if internal buffer is larger than used
 *  	  space, shrink the internal buffer to the used size
 * @param buf_t* buf   Buffer to shrink
 * @return ret_t BUFT_OK on success, a negative code otherwise
 */
__attribute__((warn_unused_result))
extern ret_t buf_shrink(buf_t *buf);


/****************************** FUNCTION: buf_t_string.c ******************************/

/**
 * @brief Test whether string buffer is valid
 * @param buf_t* buf   Buffer to test
 * @return ret_t BUFT_OK if valid, an error code otherwise
 */
__attribute__((warn_unused_result))
extern ret_t buf_str_is_valid(/*@in@*//*@temp@*/buf_t *buf);

/**
 * @brief Allocate buffer and mark it as STRING
 * @param size_t size
 * @return buf_t*
 */
__attribute__((warn_unused_result))
extern /*@null@*/ buf_t *buf_string(buf_s64_t size);

/**
 * @brief Convert given string "str" into buf_t. The resulting buf_t is a nirmal STRING type buffer.
 * @param char * str String to convert
 * @param size_t size_without_0 Length of the string without terminating '\0'
 * @return buf_t* New buf_t containing the "str"
 */
__attribute__((warn_unused_result))
extern /*@null@*/  buf_t *buf_from_string(/*@in@*//*@temp@*/char *str, const buf_s64_t size_without_0);

/**
 * @brief Add data to the string buffer
 * @param buf Pointer to the string buffer
 * @param new_data - New data to add (Unux string)
 * @param size - Size of the data to add
 * @return ret_t 
 */
__attribute__((warn_unused_result))
extern ret_t buf_str_add(/*@in@*//*@temp@*/buf_t *buf, /*@in@*//*@temp@*/const char *new_data, const buf_s64_t size);

/**
 * @brief Add string from string buffer from another string
 *  	  buffer
 * @param buf_t* buf_to  Buffer to add string
 * @param const buf_t* buf_from Buffer to add string from
 * @return ret_t BUFT_OK on success, a negative error code
 *  	   otherwise
 * @details This is an wrapper of buf_str_add()
 */
__attribute__((warn_unused_result))
extern ret_t buf_str_add_buf(/*@in@*//*@temp@*/buf_t *buf_to, buf_t *buf_from);

/**
 * @brief Experimental: detect used length of string buffer
 * @param buf - Buffer to detect string length
 * @return ret_t OK on success, 
 */
__attribute__((warn_unused_result))
extern ret_t buf_str_detect_used(/*@in@*//*@temp@*/buf_t *buf);

/**
 * @brief String-specific buffer pack function 
 * @param buf - Buffer to pack
 * @return ret_t OK on success, not OK on a failure 
 */
__attribute__((warn_unused_result))
extern ret_t buf_str_pack(/*@temp@*//*@in@*/buf_t *buf);

/**
 * @brief sprintf into buf_t
 * @param char * format Format (like in printf first argument )
 * @return buf_t*
 */
__attribute__((warn_unused_result))
extern /*@null@*/ buf_t *buf_sprintf(/*@in@*//*@temp@*/const char *format, ...);

/**
 * @brief Concat two buf_t buffers
 * @param dst - Buffer to add string from src
 * @param src - Buffer to copy from
 * @return ret_t - OK on success, EINVAL if the buffer is NULL,
 *  	   BAD if a buffer is not the string buffer
 */
__attribute__((warn_unused_result))
extern ret_t buf_str_concat(/*@in@*//*@temp@*/buf_t *dst, /*@in@*//*@temp@*/buf_t *src);


/****************************** FUNCTION: buf_t_array.c ******************************/

/**
 * @brief Get number of members kept in this buf_t
 * @param buf_t* buf   buf_t buffer to get numbet of members
 * @return buf_s32_t Number of members
 */
__attribute__((warn_unused_result))
extern buf_s32_t buf_arr_get_members_count(buf_t *buf);

/**
 * @brief Set number of members in the array
 * @param buf_t* buf        Buffer to set number of members
 * @param buf_s32_t new_members Number of members to set
 * @return ret_t OK on syccess, a negative on an error
 * @details Number of members must by 0 or it should be multiple
 *  		of buf->arr.size; otherwise an error returned
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_arr_members_count(buf_t *buf, buf_s32_t new_members);

/**
 * @brief Return size of a member in buf_t->arr
 * @param buf_t* buf   Array buffer to get size of an array
 *  		   member
 * @return buf_s32_t Size of an member of the array buffer;
 *  	   On an error a negative value returned
 */
__attribute__((warn_unused_result))
extern buf_s32_t buf_arr_get_member_size(buf_t *buf);

/**
 * @brief Set array memeber size
 * @param buf_t* buf     Buffer to set a member size
 * @param buf_s32_t new_size New size of the array member
 * @return ret_t OK on success
 * @details This is a function for a raw manupulation with a
 *  		array buffer.
 */
__attribute__((warn_unused_result))
extern ret_t buf_set_arr_member_size(buf_t *buf, buf_s32_t new_size);

/**
 * @brief Calcualate used memory
 * @param buf_t* buf   Array buffer to calculate used memory
 * @return buf_s64_t Calculated used memory
 * @details This function returns number of members * size of
 *  		one mebeber
 */
__attribute__((warn_unused_result))
extern buf_s64_t buf_arr_get_used(buf_t *buf);

/**
 * @brief Set used space; this function used internally and also
 *  	  for raw manipulation on the buffer
 * @param buf_t* buf     
 * @param buf_s64_t new_used
 * @return ret_t 
 * @details This function can be used only when a size of a
 *  		member in the array is set
 */
__attribute__((warn_unused_result))
extern ret_t buf_arr_set_used(buf_t *buf, buf_s64_t new_used);

/**
 * @brief Test different condition to detect wheather the buffer
 *  	  valid or invalid. 
 * @param buf_t* buf   Buffer to test
 * @return ret_t OK on success; a negative value on an error
 */
__attribute__((warn_unused_result))
extern ret_t buf_array_is_valid(/*@in@*//*@temp@*/buf_t *buf);

/**
 * @brief Allocate new array buffer. Allocate memory for
 *  	  'num_of_members' members of 'member_size' bytes each
 * @param buf_s32_t member_size   Size of a single member,
 *  				in bytez
 * @param buf_s32_t num_of_members Number of memebrs
 * @return buf_t* Returns allocated buffer; on error,
 *  	   NULL pointer returned
 */
__attribute__((warn_unused_result))
extern /*@null@*/ buf_t *buf_array(buf_s32_t member_size, buf_s32_t num_of_members);

/**
 * @brief Add several members at once to the buf aray
 * @param buf_t* buf   buf_t buffer to add members into
 * @param const void* new_data_ptr      Mmeory buffer containing
 *  			members to add into buf_t
 * @param const buf_s32_t num_of_new_members Number of members
 *  			should be added into buf_t
 * @return ret_t OK on success
 */
__attribute__((warn_unused_result))
extern ret_t buf_arr_add_members(buf_t *buf, const void *new_data_ptr, const buf_s32_t num_of_new_members);

/**
 * @brief Add new members as a memory with size, not number of
 *  	  members
 * @param buf_t* buf     Buffer to add new mebers as a memory
 * @param const char* new_data Pointer to memory containing
 *  			members to be added
 * @param const buf_s64_t size    Size of the mmeory in bytes
 * @return ret_t BUFT_OK on success, arror code on faulure
 * @details The size of the memory to add must be exact multiply
 *  		of one memeber size
 */
__attribute__((warn_unused_result))
extern ret_t buf_arr_add_memory(buf_t *buf, /*@temp@*//*@in@*/const char *new_data, const buf_s64_t size);

/**
 * @brief Add one member to the beffer
 * @param buf_t* buf         Buffer to add a new member
 * @param const void* new_data_ptr Pointer to memory where new
 *  			data is kept
 * @return ret_t OK on success, a negative value on an error
 */
__attribute__((warn_unused_result))
extern ret_t buf_arr_add(buf_t *buf, const void *new_data_ptr);

/**
 * @brief Remove several members from the array buffer
 * @param buf_t* buf               Buffer to remove members
 * @param const buf_s32_t from_member       Index of the first
 *  			meber to remove; The first member has index 0
 * @param const buf_s32_t num_of_new_members How mamny members
 *  			to remove. Minumum is 1, maximum is up to the
 *  			last member
 * @return ret_t OK on success
 * @details Pay attention: after members are removed,
 *  		the rest of members are shifted to close the gap.
 *  		For example: if there are 3 members [0],[1],[2],[3]
 *  		and members [1],[2] are removed, than after this
 *  		operation member [3] becomes member [1]
 */
__attribute__((warn_unused_result))
extern ret_t buf_arr_rm_members(buf_t *buf, const buf_s32_t from_member, const buf_s32_t num_of_new_members);

/**
 * @brief Remove one member from the array buffer
 * @param buf_t* buf         Buffer to remove one member
 * @param const buf_s32_t member_index Index of member to
 *  			remove; the first member has 0 index
 * @return ret_t OK on success, a negative value on an error
 * @details This function is a wrapper of the
 *  		buf_arr_rm_members(); all restructions an behavoir
 *  		applied.
 */
__attribute__((warn_unused_result))
extern ret_t buf_arr_rm(buf_t *buf, const buf_s32_t member_index);

/**
 * @brief Set new 'used' value
 * @param buf_t* buf   Buffer to set 'used' value
 * @param buf_s64_t used  New 'used' value
 * @return ret_t OK on success
 * @details Be aware: this function doesn't reallocate data;
 *  		it only sets ne value of the '->used' field.
 */
__attribute__((warn_unused_result))
extern ret_t buf_array_set_used(/*@in@*//*@temp@*/buf_t *buf, buf_s64_t used);

/**
 * @brief Return pointer to an array member
 * @param buf_t* buf         Buffer to get pointer to a member
 *  		   of the internal array
 * @param const buf_s32_t member_index Index of the mmeber to
 *  			get pointer to
 * @return void* Pointer to the start of memory of the memner;
 *  	   NULL on an error
 * @details The index must be >= 0 and <= the last member 
 */
__attribute__((warn_unused_result))
extern void *buf_arr_get_member_ptr(buf_t *buf, const buf_s32_t member_index);

/**
 * @brief Copy the member by given index to the 'dest' buffer
 * @param buf_t* buf         Array buffer to copy a member from
 * @param const buf_s32_t member_index Index of the mmeber
 * @param void* dest        Buffer to copy the mmeber to
 * @param buf_s32_t dest_memory_size - Size of the destination
 *  				buffer, must be >= size of the member
 * @return ret_t OK on success, a negative error code on an
 *  	   error
 */
__attribute__((warn_unused_result))
extern ret_t buf_arr_member_copy(buf_t *buf, const buf_s32_t member_index, void *dest, buf_s32_t dest_memory_size);

/**
 * @brief Clean the array buffer; the allocated memory is
 *  	  released.
 * @param buf_t* buf   Buffer to clean
 * @return ret_t BUFT_OK on success, a gegative error code on an
 *  	   error
 */
__attribute__((warn_unused_result))
extern ret_t buf_arr_clean(/*@temp@*//*@in@*//*@special@*/buf_t *buf);

/* Additional defines */
#ifdef BUF_DEBUG
	#define BUF_TEST(buf) do {if (0 != buf_is_valid(buf)){fprintf(stderr, "######>>> Buffer invalid here: func: %s file: %s + %d [allocated here: %s +%d %s()]\n", __func__, __FILE__, __LINE__, buf->filename, buf->line, buf->func);}} while (0)
	#define BUF_DUMP(buf) do {DD("[BUFDUMP]: [%s +%d] buf = %p, data = %p, room = %u, used = %u [allocated here: %s +%d %s()]\n", __func__, __LINE__, buf, buf->data, buf->room, buf->used, buf->filename, buf->line, buf->func);} while(0)
	#define BUF_DUMP_ERR(buf) do {DD("[BUFDUMP]: [%s +%d] buf = %p, data = %p, room = %u, used = %u [allocated here: %s +%d %s()]\n", __func__, __LINE__, buf, buf->data, buf->room, buf->used, buf->filename, buf->line, buf->func);} while(0)
#else
	#define BUF_TEST(buf) do {if (0 != buf_is_valid(buf)){fprintf(stderr, "######>>> Buffer test invalid here: func: %s file: %s + %d\n", __func__, __FILE__, __LINE__);}} while (0)
	#define BUF_DUMP(buf) do {DD("[BUFDUMP]: [%s +%d] buf = %p, data = %p, room = %u, used = %u\n", __func__, __LINE__, buf, buf->data, buf->room, buf->used);} while(0)
	#define BUF_DUMP_ERR(buf) do {DD("[BUFDUMP]: [%s +%d] buf = %p, data = %p, room = %u, used = %u\n", __func__, __LINE__, buf, buf->data, buf->room, buf->used);} while(0)

#endif

#ifndef BUF_NOISY
	#undef BUF_DUMP
	#define BUF_DUMP(buf) do{}while(0)
#endif

#endif /* _BUF_T_H_ */
