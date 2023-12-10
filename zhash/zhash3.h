#ifndef ZHASH_H
#define ZHASH_H

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "optimization.h"

/* hash table
 * keys are strings or integers
 * values are void *pointers */

#define COUNT_OF(arr) (sizeof(arr) / sizeof(*arr))
#define zfree free

/* This is the max length of the String Key;
   we expect no key will exceeds it */
#define ZHASH_STRING_KEY_MAX_LEN (64)

/* If we use 32 bit integer for key, the collision probability is high.
   My tests show that the first collision happens after 1,187,966 items inserted into zhash */
typedef struct {
	char *key_str; /**< String key representation */
	uint32_t key_str_len; /**< Length of the key string */
	uint64_t key_int64; /**< The integer calculated from the key string; this is the main value for add / search operations */
} basket_key_t;

typedef struct {
	void *val; /**< Value, a pointer to a buffer */
	uint32_t val_size; /**< Size of the value buffer pointer by val */
} basket_val_t;

/* struct representing an entry in the hash table */
typedef struct ZHashEntry {
	basket_key_t Key; /**< Conatains key; the key could be string and int, or int only */
	basket_val_t Val; /**< Contains value + size of value */
	struct ZHashEntry *next; /**< The next entry */
} zentry_t;

/**
 * @author Sebastian Mountaniol (7/30/22)
 * @brief struct representing the hash table
  size_index is an index into the hash_sizes array in hash.c 
 * @details 
 */
typedef struct __attribute__((packed)){
	uint32_t size_index; /**< one of predefined value, a primary number, see ::hash_sizes in zhash3.c */
	uint32_t entry_count; /**< Number of entries added into hash table */
	zentry_t **entries; /**< Array of entries */
}
ztable_t;

/**
 * @def ZHASH_WATERMARK - contains predefined pattern for
 *  	zhash_header_t structure
 */
#define ZHASH_WATERMARK (0xFAFA7777)
/**
 * @def ZENTRY_WATERMARK - contains predefined pattern for
 *  	zhash_entry_t structure
 */
#define ZENTRY_WATERMARK (0x898AE990)

/**
 * @author Sebastian Mountaniol (7/30/22)
 * @brief This structure used to pack zhash table into a flat
 *  	  memory structure; all entries use another structure,
 *  	  see ::zhash_entry_t
 * @details 
 */
typedef struct __attribute__((packed)){
	uint32_t watemark; /**< Contains predefined pattern, see ::ZENTRY_WATERMARK */
	uint32_t checksum;
	uint32_t entry_count;
}
zhash_header_t;

/**
 * @author Sebastian Mountaniol (7/30/22)
 * @brief This structure used to pack entries into a flat memory
 *  	  structure 
 * @details 
 */
typedef struct __attribute__((packed)){
	uint32_t watemark; /**< Predefined value, for validation */
	uint32_t checksum; /**< Checksum of this entry, not implemented yet*/
	uint64_t key_int64; /**< Key: integer */
	uint32_t key_str_len; /**< Length of the 'char *key', not includes terminating \0 */
	uint32_t val_size; /**< Length of the entry */
}
zhash_entry_t;



/*** API functions ***/


/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func ztable_t* zcreate_hash_table(void)
 * @brief Create new empty hash table
 * @param void
 * @return ztable_t* Pointer to new hash table on success, NULL on error
 * @details
 */
__attribute__((warn_unused_result))
ztable_t *zhash_allocate(void);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @brief Release hash table.
 * @param ztable_t * hash_table Hash table to free
 * @param const int force_values_clean If not 0, force releasing
 *  			the user buffers kept in the ztable
 * @param const int8_t force_values_clean - If not 0, release
 *  			aslo all values bu passing them to free().
 * @details If the hash table is not empty, all entries will be released as well.
 *          The data kept in the entries not released
 */
void zhash_release(ztable_t *hash_table, const int8_t force_values_clean);

/* hash operations */

/* Set of function wehere the key is a string */

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void zhash_insert_by_str(ztable_t *hash_table, char *key, void *val)
 * @brief Insert new entry into the hash table. The key is string.
 * @param ztable_t * hash_table 	A hash table to insert the new entry into
 * @param char *key_str 			The key to used for insert / search
 * @param const size_t key_str_len 	Length of the key
 * @param void *val val 			Pointer to data
 * @param const size_t val_len		Length of the buffer pointed by 'val'
 * @details The 'key_str' string will be duplicated.
 *  		The zhash does not use user's string,
 *  		and the caller can release the string after call.
 */
__attribute__((warn_unused_result, hot))
int8_t zhash_insert_by_str(ztable_t *hash_table,
						char *key_str,
						const size_t key_str_len,
						void *val,
						const size_t val_size);
/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void* zhash_find_by_str(ztable_t *hash_table, char *key)
 * @brief Find an entry in hash table
 * @param ztable_t * hash_table The hash table to search
 * @param char * key_str A null terminated string to use as key
 *  		   for searching
 * @param size_t key_str_len - Length of the string key without
 *  			 terminating '\0', equal to strlen(key_str)
 * @param ssize_t *val_size In this variable the size (bytes) of
 *  			  value will be returned
 * @return void* Pointer to data kept in the hash table, NULL if not found
 * @details The found entry will be not released
 */
__attribute__((warn_unused_result, hot))
void *zhash_find_by_str(const ztable_t *hash_table, char *key_str, const size_t key_str_len, ssize_t *val_size);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void* zhash_extract_by_str(ztable_t *hash_table, char *key)
 * @brief Find and extract data from the hash table using string as the search key
 * @param ztable_t * hash_table The hash table to search in
 * @param char * key A null terminated string to use for search
 * @param size_t key_str_len - Length of the string key without
 *  			 terminating '\0', equal to strlen(key_str)
 * @param ssize_t *size In this variable the size of Val is
 *  			  returned
 * @return void* Data kept in hash table, NULL if not found
 * @details This function removes the found entry from the hash and returns data to caller
 */
__attribute__((warn_unused_result, hot))
void *zhash_extract_by_str(ztable_t *hash_table, const char *key_str, const size_t key_str_len, ssize_t *size);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func bool zhash_exists_by_str(ztable_t *hash_table, char *key)
 * @brief Check if an entry for the given key exists in the hash table
 * @param ztable_t * hash_table The hash table to test
 * @param char * key A null terninated string to search
 * @param size_t key_str_len - Length of the string key without
 *  			 terminating '\0', equal to strlen(key_str)
 * @return bool True if a record for the given key presens, false if doesnt
 * @details
 */
__attribute__((warn_unused_result, pure, hot))
bool zhash_exists_by_str(ztable_t *hash_table, const char *key_str, size_t key_str_len);

/* Set of function where the key is an integer */

/**
 * @author Sebastian Mountaniol (7/28/22)
 * @brief Generate integer key (hash) from the string 
 * @param const char* key_str   String
 * @param const size_t key_str_len   String length
 * @return uint64_t Calculated hash (key value)
 * @details BE AWARE: This is an internal function. No values
 *  		validation.
 */
__attribute__((warn_unused_result, pure, hot))
uint64_t zhash_key_int64_from_key_str(const char *key_str, const size_t key_str_len);


/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void zhash_insert_by_str(ztable_t *hash_table, char *key, void *val)
 * @brief Insert new entry into the hash table. The key is integer.
 * @param ztable_t * hash_table A hash table to insert the new entry into
 * @param uint64_t key The key to use for insert / search
 * @param void * val Pointer to data
 * @return 0 if inserted, 1 if there is a collision, -1 on an error
 * @details
 */
__attribute__((warn_unused_result, hot))
int8_t zhash_insert_by_int(ztable_t *hash_table, uint64_t int_key, void *val, size_t val_size);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void *zhash_find_by_int(ztable_t *hash_table, uint64_t
 *  	 key)
 * @brief Find an entry in hash table
 * @param ztable_t * hash_table The hash table to search
 * @param uint64_t key An integer value to use as key for
 *  			   searching
 * @param ssize_t *val_size In this variable the size (bytes) of
 *  			  value will be returned
 * @return void* A pointer to data kept in the hash table, NULL if not found
 * @details The found entry will be deleted from the table
 */
__attribute__((warn_unused_result, hot))
void *zhash_find_by_int(const ztable_t *hash_table, uint64_t key_int64, ssize_t *val_size);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void *zhash_extract_by_int(ztable_t *hash_table,
 *  	 uint64_t key)
 * @brief Find and extract data from the hash table using an integer as the search key
 * @param ztable_t * hash_table The hash table to search in
 * @param uint64_t key An integer value to use for search
 * ssize_t *size In this variable the size of value returned
 * @return void* Data kept in hash table, NULL if not found
 * @details This function removes the found entry from the hash and returns data to caller
 */
__attribute__((warn_unused_result, hot))
void *zhash_extract_by_int(ztable_t *hash_table, const uint64_t key_int64, ssize_t *size);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func bool zhash_exists_by_int(ztable_t *hash_table, uint64_t
 *  	 key)
 * @brief Check if an entry for the given key exists in the hash table
 * @param ztable_t * hash_table The hash table to test
 * @param uint64_t key An integer value to search
 * @return bool True if the a record for the given key presens, false if doesnt
 * @details
 */
__attribute__((warn_unused_result, pure, hot))
bool zhash_exists_by_int(const ztable_t *hash_table, const uint64_t key_int64);

/**
 * @author Sebastian Mountaniol (11/3/21)
 * zentry_t* zhash_list(ztable_t *hash_table, size_t *index, zentry_t *entry)
 * 
 * @brief Iterate over all entries in zhash
 * @param hash_table - The hash table to iterate
 * @param index - Pointer to index, must be inited by caller as
 *  						0
 * @param entry - The pointer of the previously returned
 *  								 entry, must be passed every time
 * 
 * @return zentry_t* - A pointer to an entry, or NULL when
 *  			 no more entries
 * @details Be careful! This function return zentry_t! You
 *  				should use entry->val to get the value saved in hash
 */
__attribute__((warn_unused_result, cold))
zentry_t *zhash_list(const ztable_t *hash_table, size_t *index, const zentry_t *entry);

/**
 * @author Sebastian Mountaniol (7/28/22)
 * @brief This function calculates the size of the buffer (in
 *  	  bytes) enough to contain a flat zhash dump buffer
 * @param ztable_t* hash_table
 * @return size_t Size of needded buffer, in bytes
 * @details 
 */
__attribute__((warn_unused_result, pure, nonnull(1)))
size_t zhash_to_buf_allocation_size(const ztable_t *hash_table);

/**
 * @author Sebastian Mountaniol (7/27/22)
 * @brief Create a flat buffer from the zhash table. You can
 *  	  restore zhash from this memory buffer with function
 *  	  ::zhash_from_buf()
 * @param ztable_t* hash_table Hash to dump into memory buffer
 * @param size_t* size  The size of resulting buffer will be
 *  			returned in this variable
 * @return void* Poiter to the new buffer. In case of error a
 *  	   NULL returned.
 * @details The original zhash not affected by this operation,
 *  		you can use it or release it, by your choice.
 */

__attribute__((warn_unused_result))
extern void *zhash_to_buf(const ztable_t *hash_table, size_t *size);

/**
 * @author Sebastian Mountaniol (7/27/22)
 * @brief Create zhash table from the flat memory buffer. The
 *  	  flat memory buffer must be a result of
 *  	  ::zhash_to_buf() function
 * @param void* buf   Flat memory buffer containing a dump of
 *  		  zhash table
 * @param size_t size  Size of the flat memory buffer
 * @return ztable_t* zhash object, restored from the buffer.
 *  	   NULL on an error.
 * @details 
 */
__attribute__((warn_unused_result))
extern ztable_t *zhash_from_buf(const char *buf, const size_t size);


/**
 * @author Sebastian Mountaniol (7/31/22)
 * @brief For test: compare two zhash tables
 * @param const ztable_t* left  The first zhash table
 * @param const ztable_t* right The second zhash table
 * @return int 0 if they equal, -1 on an error, 1 if they are
 *  	   not equal
 * @details 
 */
__attribute__((warn_unused_result, cold))
extern int8_t zhash_cmp_zhash(const ztable_t *left, const ztable_t *right);

#ifdef DEBUG3
void zhash_dump(const ztable_t *hash_table, const char *name);
#else
void zhash_dump(const ztable_t *hash_table, __attribute__((unused))const char *name);
#endif

#endif

