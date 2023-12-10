#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "debug.h"
#include "tests.h"
#include "zhash3.h"
#include "checksum.h"
#include "optimization.h"

/* possible sizes for hash table; must be prime numbers */
/**
 * @author Sebastian Mountaniol (8/1/22)
 * @brief This is an array of possible hash table sizes, all prime numbers
 * @details This table is translasion table from the 'ztable_t->size_index' to the number of entries in the zhash.
 * 			So if the 'size_index' is '1' it means there are 53 elements in ztable_t->entries array
 */
static const size_t hash_sizes[] = {
	53, 101, 211, 503, 1553, 3407, 6803, 12503, 25013, 50261,
	104729, 250007, 500009, 1000003, 2000029, 4000037, 10000019,
	25000009, 50000047, 104395301, 217645177, 512927357, 1000000007
};

/*** STATIC FUNCTIONS ***/

__attribute__((warn_unused_result, hot))
static void *zmalloc(const size_t sz)
{
	return calloc(1, sz);
}

__attribute__((warn_unused_result, const))
static size_t next_size_index(const size_t size_index)
{
	if (size_index == COUNT_OF(hash_sizes)) return (size_index);
	return (size_index + 1);
}

__attribute__((warn_unused_result, const))
static size_t previous_size_index(const size_t size_index)
{
	if (size_index == 0) return (size_index);
	return (size_index - 1);
}

__attribute__((warn_unused_result, hot))
static void *zcalloc(const size_t num, const size_t size)
{
	void *ptr = calloc(num, size);
	if (!ptr) exit(EXIT_FAILURE);
	return (ptr);
}

/**
 * @author Sebastian Mountaniol (8/1/22)
 * @brief Create a zhash table with asked 'size_index'; see
 *  	  ::hash_sizes[] array 
 * @param const size_t size_index
 * @return ztable_t* 
 * @details 
 */
__attribute__((warn_unused_result))
static ztable_t *zcreate_hash_table_with_size(const size_t size_index)
{
	ztable_t *hash_table = zmalloc(sizeof(ztable_t));
	TESTP(hash_table, NULL);

	hash_table->size_index = size_index;
	hash_table->entry_count = 0;
	hash_table->entries = zcalloc(hash_sizes[size_index], sizeof(void *));
	if (NULL == hash_table->entries) {
		DE("Could not allocate %zu entries\n", hash_sizes[size_index]);
		free(hash_table);
		return NULL;
	}
	return (hash_table);
}

/**
 * @author Sebastian Mountaniol (7/31/22)
 * @brief Generate hash, means: index in zhash->entries array
 * @param const ztable_t* hash_table Pointer to hash table
 *  			struct
 * @param const uint64_t key_int64 Integer key
 * @return size_t Hash, the array index in zhash->entries
 * @details BE AWARE: This is an internal function. No values
 *  		validation.
 */
__attribute__((warn_unused_result, pure, nonnull(1)))
static size_t zhash_entry_index_by_int(const ztable_t *hash_table, const uint64_t key_int64)
{
	const size_t size = hash_sizes[hash_table->size_index];
	return (key_int64 % size);
}

/**
 * @author Sebastian Mountaniol (7/29/22)
 * @brief Rebuild the hash to the new size
 * @param ztable_t* hash_table Hash table 
 * @param const size_t size_index New size
 * @details BE AWARE: This is an internal function. No values
 *  		validation.
 */
__attribute__((nonnull(1)))
static void zhash_rehash(ztable_t *hash_table, const size_t size_index)
{
	size_t   hash;
	size_t   size;
	size_t   ii;
	zentry_t **entries;

	if (size_index == hash_table->size_index) return;

	size = hash_sizes[hash_table->size_index];
	entries = hash_table->entries;

	hash_table->size_index = size_index;
	hash_table->entries = zcalloc(hash_sizes[size_index], sizeof(void *));

	for (ii = 0; ii < size; ii++) {
		zentry_t *entry;

		entry = entries[ii];
		while (entry) {
			zentry_t *next_entry;
			hash = zhash_entry_index_by_int(hash_table, entry->Key.key_int64);
			next_entry = entry->next;
			entry->next = hash_table->entries[hash];
			hash_table->entries[hash] = entry;
			entry = next_entry;
		}
	}
	zfree(entries);
}

/**
 * @author Sebastian Mountaniol (8/1/22)
 * @brief For debugging: print out the zhash contenent.
 * @param const ztable_t* hash_table
 * @param const char* name      
 * @details BE AWARE: This is an internal function. No values
 *  		validation. 
 */
__attribute__((nonnull(1, 2), cold))
#ifdef DEBUG3
void zhash_dump(const ztable_t *hash_table, const char *name)
#else
void zhash_dump(const ztable_t *hash_table, __attribute__((unused))const char *name)
#endif
{
	size_t   size;
	size_t   ii;
	zentry_t **entries;

	size = hash_sizes[hash_table->size_index];
	entries = hash_table->entries;

	DDD("****************************************\n");
	DDD("ZHASH: DUMP: %s\n", name);
	DDD("ZHASH: addr: %p, enties arr addr: %p, num of entries: %u\n", hash_table, hash_table->entries, hash_table->entry_count);

	for (ii = 0; ii < size; ii++) {
		zentry_t *entry;

		entry = entries[ii];
		while (entry) {
			DDD("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
			DDD(">>> Entry: %p, ->next: %p, key_int: %lX, key_str: |%s|, key_str_len: %u, val: %p, val_size: %u\n",
				entry, entry->next, entry->Key.key_int64, entry->Key.key_str, entry->Key.key_str_len, entry->Val.val, entry->Val.val_size);
			entry = entry->next;
		}
	}

	DDD("========================================\n");
}

/**
 * @author Sebastian Mountaniol (7/29/22)
 * @brief Fill zentry structure 
 * @param zentry_t* entry      	Structure to fill
 * @param uint64_t key_int64    Integer key, must be
 *  				calculated before this call
 * @param void* val        		Value to keep in the entry
 * @param const size_t val_size Size (in bytes) of the value to
 *  			keep
 * @param char* key_str    		String key; can by NULL
 * @param const size_t key_str_len	Size (length) of the string
 *  			key. No includes \0 terminator, i.e. equal to
 *  			strlen(key_str)
 * @details BE AWARE: This is an internal function. No values
 *  		validation.
 */
__attribute__((hot))
static void zentry_t_fill(zentry_t *entry, uint64_t key_int64,
						  void *val,
						  const size_t val_size,
						  char *key_str,
						  const size_t key_str_len)
{
	entry->Key.key_int64 = key_int64;
	entry->Key.key_str = key_str;
	entry->Key.key_str_len = key_str_len;
	entry->Val.val = val;
	entry->Val.val_size = val_size;
	entry->next = NULL;
}

__attribute__((warn_unused_result, hot))
static zentry_t *zentry_t_alloc(uint64_t key_int64, void *val, size_t val_size, char *key_str, size_t key_str_len)
{
	zentry_t *entry = zmalloc(sizeof(zentry_t));
	TESTP(entry, NULL);

	zentry_t_fill(entry, key_int64, val, val_size, key_str, key_str_len);
	return (entry);
}

__attribute__((nonnull(1)))
static void zentry_t_release(zentry_t *entry, const bool recursive, const int8_t force_values_clean)
{
	if (recursive && entry->next) {
		zentry_t_release(entry->next, recursive, force_values_clean);
	}

	if (NULL != entry->Key.key_str) {
		DDD("Going to release entry: %p\n", entry);
		DDD("Going to release entry->Key.key_str: %p\n", entry->Key.key_str);
		zfree(entry->Key.key_str);
		entry->Key.key_str = NULL;

		/* If asked to clean the values than release it as well: */
		if (force_values_clean && NULL != entry->Val.val) {
			zfree(entry->Val.val);
		}
	}

	/* Release the entry itself */
	zfree(entry);
}

/* Internal generic insert. Except all values for an entry. ALways insert by ineger key */
/**
 * @author Sebastian Mountaniol (8/1/22)
 * @brief Internal generic insert. Get all fields of an entry, create an entry,
 *        fill and insert into the zhash table 
 * @param ztable_t* hash_table The hash table to inset the entry into
 * @param uint64_t key_int64  User's or calculated from the string integer key
 * @param char* key_str    If user inserts by string key, the string key 
 * @param const size_t key_str_len If user inserts by string key, the string key  length without terminating \0
 * @param void* val        The pointer the user wants to keep in the zhash
 * @param const size_t val_size   Size of the value buffer.
 * @return int8_t OK if inserted successfully, -1 on an error, 1 on key collision
 * @details BE AWARE: Collisions are possible. Since 64 bits valus is used, the collision
 *          probability is low but possible. Always test the return value for collision situation.
 */
__attribute__((warn_unused_result, nonnull(1), hot))
static int8_t zhash_insert(ztable_t *hash_table,
						   uint64_t key_int64,
						   char *key_str,
						   const size_t key_str_len,
						   void *val,
						   const size_t val_size)
{
	size_t       size;
	zentry_t     *entry;
	const size_t hash   = zhash_entry_index_by_int(hash_table, key_int64);
	entry = hash_table->entries[hash];

	while (entry) {
		/* If existing such an entry, replace its content */
		if (key_int64 == entry->Key.key_int64) {
			DD("Found the item: old key %s / %lX, new %s / %lX\n",
			   entry->Key.key_str,
			   entry->Key.key_int64,
			   (key_str) ? key_str : "NULL",
			   key_int64);
			return 1;
		}
		entry = entry->next;
	}

	entry = zentry_t_alloc(key_int64, val, val_size, key_str, key_str_len);

	entry->next = hash_table->entries[hash];
	hash_table->entries[hash] = entry;
	hash_table->entry_count++;

	size = hash_sizes[hash_table->size_index];

	if (hash_table->entry_count > size / 2) {
		zhash_rehash(hash_table, next_size_index(hash_table->size_index));
	}
	return 0;
}

/**
 * @author Sebastian Mountaniol (8/1/22)
 * @brief An internal function: find and return zentry_t
 *  	  structure by integer key. Used in tests.
 * @param const ztable_t* hash_table The zhash table to search
 *  			by integer key
 * @param const uint64_t key_int64 The integer key
 * @return zentry_t* Pointer to the entry on success, NULL on
 *  	   failure.
 * @details 
 */
__attribute__((warn_unused_result, pure, nonnull(1)))
static zentry_t *zhash_find_entry_by_int(const ztable_t *hash_table, const uint64_t key_int64)
{
	zentry_t     *entry;
	const size_t hash   = zhash_entry_index_by_int(hash_table, key_int64);
	entry = hash_table->entries[hash];
	DDD("Search for key: %lX\n", key_int64);
	while (entry) {
		if (key_int64 == entry->Key.key_int64) {
			break;
		}
		entry = entry->next;
	}

	if (entry) {
		DDD("Found key: %lX, the str key: %s\n", key_int64, entry->Key.key_str);
	} else {
		DDD("For key: %lX, no entry found, returning NULL\n", key_int64);
	}
	return entry;
}

/**
 * @author Sebastian Mountaniol (8/1/22)
 * @brief An internal function: search and return zentry_t
 *  	  struct by given string key. Used in tests.
 * @param const ztable_t* hash_table The zhash table to search
 *  			by string key
 * @param char* key_str  The string key  
 * @param const size_t key_str_len The string key length
 *  			excludin terminating \0
 * @return void* Pointer to the entry, NULL on error
 * @details 
 */
__attribute__((warn_unused_result, pure, nonnull(1)))
static void *zhash_entry_find_by_str(const ztable_t *hash_table, char *key_str, const size_t key_str_len)
{
	uint64_t key_int64 = zhash_key_int64_from_key_str(key_str, key_str_len);
	DDD("Calculated key_int: %lX\n", key_int64);
	return zhash_find_entry_by_int(hash_table, key_int64);
}


/*** END OF STATIC FUNCTIONS ***/

__attribute__((warn_unused_result))
ztable_t *zhash_allocate(void)
{
	return (zcreate_hash_table_with_size(0));
}

void zhash_release(ztable_t *hash_table, const int8_t force_values_clean)
{
	size_t size;
	size_t ii;

	size = hash_sizes[hash_table->size_index];

	for (ii = 0; ii < size; ii++) {
		zentry_t *entry;

		if ((entry = hash_table->entries[ii])) {
			DDD("Going to release entry: %p\n", entry);
			zentry_t_release(entry, true, force_values_clean);
		}
	}

	zfree(hash_table->entries);
	zfree(hash_table);
}

__attribute__((warn_unused_result, pure, hot))
uint64_t zhash_key_int64_from_key_str(const char *key_str, const size_t key_str_len)
{
	uint64_t key_int64 = 0;
	if (0 == key_str_len) {
		DE("Wrong arguments: key_str = %p, key_str_len = %zu\n", key_str, key_str_len);
		abort();
	}

	if (0 != checksum_buf_to_64_bit(key_str, key_str_len, &key_int64)) {
		DE("Could not calculate the 64 bit key\n");
		abort();
	}
	return key_int64;
}

__attribute__((warn_unused_result, hot))
int8_t zhash_insert_by_int(ztable_t *hash_table, uint64_t int_key, void *val, size_t val_size)
{
	return zhash_insert(hash_table, int_key, NULL, 0, val, val_size);
}

__attribute__((warn_unused_result, hot))
int8_t zhash_insert_by_str(ztable_t *hash_table,
						   char *key_str,
						   const size_t key_str_len,
						   void *val,
						   const size_t val_size)
{
	char     *key_str_copy;
	uint64_t key_int64     = zhash_key_int64_from_key_str(key_str, key_str_len);

	DDD("Calculated key_int: %lX\n", key_int64);
	key_str_copy = strndup(key_str, key_str_len);
	if (NULL == key_str_copy) {
		DE("Could not duplicate string key");
		abort();
	}
	return zhash_insert(hash_table, key_int64, key_str_copy, key_str_len, val, val_size);
}

__attribute__((warn_unused_result, hot))
void *zhash_find_by_int(const ztable_t *hash_table, uint64_t key_int64, ssize_t *val_size)
{
	zentry_t     *entry;
	const size_t hash   = zhash_entry_index_by_int(hash_table, key_int64);
	entry = hash_table->entries[hash];

	while (entry && (key_int64 != entry->Key.key_int64)) entry = entry->next;

	if (NULL == entry) {
		*val_size = 0;
		return NULL;
	}

	*val_size = (ssize_t)entry->Val.val_size;
	return entry->Val.val;
}

/* TODO: Convert string to int64 key and search by int64 key */
__attribute__((warn_unused_result, hot))
void *zhash_find_by_str(const ztable_t *hash_table, char *key_str, const size_t key_str_len, ssize_t *val_size)
{
	uint64_t key_int64 = zhash_key_int64_from_key_str(key_str, key_str_len);
	DDD("Calculated key_int: %lX\n", key_int64);
	return zhash_find_by_int(hash_table, key_int64, val_size);

}

__attribute__((warn_unused_result, hot))
void *zhash_extract_by_int(ztable_t *hash_table, const uint64_t key_int64, ssize_t *out_size)
{
	size_t       size;
	zentry_t     *entry;
	void         *val;
	const size_t hash   = zhash_entry_index_by_int(hash_table, key_int64);
	entry = hash_table->entries[hash];

	if (entry && key_int64 == entry->Key.key_int64) {
		hash_table->entries[hash] = entry->next;
	} else {
		while (entry) {
			if (entry->next && (key_int64 == entry->next->Key.key_int64)) {
				zentry_t *deleted_entry;

				deleted_entry = entry->next;
				entry->next = entry->next->next;
				entry = deleted_entry;
				break;
			}
			entry = entry->next;
		}
	}

	if (!entry) return (NULL);

	val = entry->Val.val;
	*out_size = entry->Val.val_size;
	zentry_t_release(entry, false, 0);
	hash_table->entry_count--;

	size = hash_sizes[hash_table->size_index];

	if (hash_table->entry_count < size / 8) {
		zhash_rehash(hash_table, previous_size_index(hash_table->size_index));
	}

	return (val);
}

__attribute__((warn_unused_result, hot))
void *zhash_extract_by_str(ztable_t *hash_table, const char *key_str, const size_t key_str_len, ssize_t *size)
{
	const uint64_t key_int64 = zhash_key_int64_from_key_str(key_str, key_str_len);
	return zhash_extract_by_int(hash_table, key_int64, size);
}

__attribute__((warn_unused_result, pure, hot))
bool zhash_exists_by_int(const ztable_t *hash_table, const uint64_t key_int64)
{
	zentry_t     *entry;
	const size_t hash   = zhash_entry_index_by_int(hash_table, key_int64);
	entry = hash_table->entries[hash];

	while (entry && key_int64 != entry->Key.key_int64) entry = entry->next;
	if (entry) {
		return true;
	}

	return false;
}

__attribute__((warn_unused_result, pure, hot))
bool zhash_exists_by_str(ztable_t *hash_table, const char *key_str, size_t key_str_len)
{
	const uint64_t key_int64 = zhash_key_int64_from_key_str(key_str, strnlen(key_str, key_str_len));
	return zhash_exists_by_int(hash_table, key_int64);
}

/*** Iterate all items in hash ***/
__attribute__((warn_unused_result, cold))
zentry_t *zhash_list(const ztable_t *hash_table, size_t *index, const zentry_t *entry)
{
	size_t   size;
	TESTP(hash_table, NULL);
	TESTP(index, NULL);

	/* Get numeric size of the hash table */
	size = hash_sizes[hash_table->size_index];

	/* If there is a next member in this hash table cell, just return */
	if (NULL != entry && entry->next) {
		return entry->next;
	}

	/* No more entried in the linked list, advance index */
	(*index)++;

	/* Test all entries, until a filled index found in the array */
	while (*index < size) {
		/* Is there an entry? Return it */
		if (hash_table->entries[*index]) {
			return hash_table->entries[*index];
		}

		/* No entry here; advance the index */
		(*index)++;
	}
	return NULL;
}

/*** ADDITION: ZHASH TO BUF / BUF TO ZHASH ***/
__attribute__((warn_unused_result, pure, nonnull(1)))
size_t zhash_to_buf_allocation_size(const ztable_t *hash_table)
{
	/* We need one header for the whole buffer */
	size_t       size           = sizeof(zhash_header_t);
	size_t       index;
	const size_t num_of_entryes = hash_sizes[hash_table->size_index];

	/* Per entry we need entry header */

	/* Now run on all entries and count data size */
	for (index = 0; index < num_of_entryes; index++) {
		zentry_t *entry = hash_table->entries[index];
		/* Is there an entry? Return it */
		while (entry) {
			size += sizeof(zhash_entry_t);
			size += entry->Key.key_str_len;
			size += entry->Val.val_size;
			entry = entry->next;
		}
	}

	return size;
}

__attribute__((warn_unused_result))
void *zhash_to_buf(const ztable_t *hash_table, size_t *size)
{
	size_t         index;
	size_t         num_of_entryes;
	size_t         offset         = 0;
	zhash_header_t *zheader;

	char           *buf;
	DDD("Start\n");
	*size = zhash_to_buf_allocation_size(hash_table);
	DDD("Calculated size: %zu\n", *size);

	buf = malloc(*size);
	memset(buf, 0, *size);
	zheader = (zhash_header_t *)buf;
	zheader->entry_count = hash_table->entry_count;
	zheader->watemark = ZHASH_WATERMARK;
	zheader->checksum = 0;

	offset += sizeof(zhash_header_t);

	num_of_entryes = hash_sizes[hash_table->size_index];

	/* Now run on all entries and count data size */
	//for (index = 0; index < *size; index++) {
	for (index = 0; index < num_of_entryes; index++) {
		zentry_t *entry = hash_table->entries[index];
		DDD("Entry by index %zu of %zu\n", index, num_of_entryes);
		/* Is there an entry? Return it */
		while (entry) {
			/* Advance the pointer */
			zhash_entry_t  *zentry = (zhash_entry_t *)(buf + offset);
			zentry->watemark = ZENTRY_WATERMARK;
			zentry->checksum = 0;
			zentry->key_str_len = entry->Key.key_str_len;
			zentry->key_int64 = entry->Key.key_int64;
			zentry->val_size = entry->Val.val_size;

			/* Now, dump the string key (if any) and val (if any) */
			offset += sizeof(zhash_entry_t);

			if (entry->Key.key_str && (0 == entry->Key.key_str_len)) {
				DE("Wrong: entry->Key.key_str != NULL but entry->Key.key_str_len = 0\n");
				abort();
			}

			if (NULL == entry->Key.key_str && (entry->Key.key_str_len > 0)) {
				DE("Wrong: entry->Key.key_str == NULL but entry->Key.key_str_len > 0\n");
				abort();
			}

			if (entry->Key.key_str) {
				memcpy(buf + offset, entry->Key.key_str, entry->Key.key_str_len);
				offset += entry->Key.key_str_len;
			}

			if (entry->Val.val) {
				memcpy(buf + offset, entry->Val.val, entry->Val.val_size);
				offset += entry->Val.val_size;
			}

			entry = entry->next;
		}
	}

	DDD("Offset: %zu, size : %zu\n", offset, *size);
	return buf;
}

__attribute__((warn_unused_result, pure))
static int8_t zhash_is_valid(const char *buf, const size_t size)
{
	const zhash_header_t *zhead = (zhash_header_t *)buf;
	TESTP(buf, -1);

	if (size < sizeof(zhash_header_t)) {
		DE("Wrong size, too small\n");
		return -1;
	}

	if (ZHASH_WATERMARK != zhead->watemark) {
		DE("Bad watermark in zhash_header_t: expected %X but it is %X\n", ZHASH_WATERMARK, zhead->watemark);
		return -1;
	}
	return 0;
}

__attribute__((warn_unused_result))
ztable_t *zhash_from_buf(const char *buf, const size_t size)
{
	size_t               index;
	size_t               offset = 0;
	const zhash_header_t *zhead = (zhash_header_t *)buf;

	if (zhash_is_valid(buf, size)) {
		DE("Zhash flat buffer is invalid\n");
		abort();
	}

	/* From the header we know the count of entries in the zhash table */
	ztable_t *zt = zhash_allocate();
	TESTP(zt, NULL);

	offset += sizeof(zhash_header_t);

	for (index = 0; index < zhead->entry_count; index++) {
		int8_t              rc;
		char                *key_str = NULL;
		void                *val;
		const zhash_entry_t *zent    = (zhash_entry_t  *)(buf + offset);
		offset += sizeof(zhash_entry_t);

		/* Set watermark */
		if (ZENTRY_WATERMARK != zent->watemark) {
			DE("Bad watermark in zhash_entry_t: expected %X but it is %X\n", ZENTRY_WATERMARK, zent->watemark);
		}

		/* Extract ket string, if any */
		if (zent->key_str_len > 0) {
			key_str = zmalloc(zent->key_str_len + 1);
			/* The string key, if exists, placed right after the zhash_entry_t struct */
			memcpy(key_str, (buf + offset), zent->key_str_len);
			offset += zent->key_str_len;
		}

		val = malloc(zent->val_size);
		/* Extract the value into the buffer */
		memcpy(val, (buf + offset), zent->val_size);
		offset += zent->val_size;

		DDD("Inserting: zt = %p, zent->key_int64 = %lX, key_str = |%s|, zent->key_str_len = %u, val = %p, zent->val_size = %u\n",
			zt, zent->key_int64,
			(NULL != key_str) ? key_str : "NULL",
			zent->key_str_len, val, zent->val_size);

		rc = zhash_insert(zt, zent->key_int64, key_str, zent->key_str_len, val, zent->val_size);
		if (rc < 0) {
			DE("Error on a new entry insert (extracted frpm buf) into new zhash\n");
			abort();
		}
		if (rc > 0) {
			DE("Collision on a new entry insert (extracted frpm buf) into new zhash\n");
			abort();
		}
	}

	DDD("Offset: %zu, size : %zu\n", offset, size);

	if (offset != size) {
		DE("Size of extracted zhash (%zu) is not what expected (%zu)\n", offset, size);
		TRY_ABORT();
	}

	zhash_dump(zt, "RESTORED FROM FLAT BUFFER");
	return zt;


}

/* Compare two zhash buffers */
__attribute__((warn_unused_result, cold))
int8_t zhash_cmp_zhash(const ztable_t *left, const ztable_t *right)
{

	uint32_t index;
	uint32_t zhash_arr_size;

	TESTP(left, -1);
	TESTP(right, -1);

	if (left->entry_count != right->entry_count) {
		DDD("Not same num of entries: left %u, right %u\n", left->entry_count, right->entry_count);
		return -1;
	}

	if (0 == left->entry_count) {
		DDD("Entry count is 0, so hash tables are equial\n");
		return 0;
	}
	zhash_dump(left, "LEFT");
	zhash_dump(right, "RIGHT");

	zhash_arr_size = hash_sizes[left->size_index];

	for (index = 0; index < zhash_arr_size; index++) {
		zentry_t *entry_left = left->entries[index];

		DDD("BEFORE WHILE: Entry left: %p, left->next: %p\n", entry_left, (entry_left) ? entry_left->next : 0);

		while (entry_left) {
			/* Search for the entry with the same key in the right zhash */
			zentry_t *entry_right = zhash_find_entry_by_int(right, entry_left->Key.key_int64);

			DDD(">>> Entry left: %p, left->next: %p\n", entry_left, entry_left->next);

			/*** TEST 1: The record from the left not found in the right ***/

			if (NULL == entry_right) {
				DDD("For left entry no right entry: int key %lX, str entry %s\n",
					entry_left->Key.key_int64, entry_left->Key.key_str);
				return 1;
			}

			DDD(">>> Entry Left: %p, ->next: %p, key_int: %lX, key_str: |%s|\n",
				entry_left, entry_left->next, entry_left->Key.key_int64, entry_left->Key.key_str);

			DDD(">>> Entry Right: %p, ->next: %p, key_int: %lX, key_str: |%s|\n",
				entry_right, entry_right->next, entry_right->Key.key_int64, entry_right->Key.key_str);

			/*** TEST 2: The left's string length not match right's ***/

			if (entry_left->Key.key_str_len != entry_right->Key.key_str_len) {
				DDD("Left->string key len not match Right->string key len : %u != %u\n",
					entry_left->Key.key_str_len,
					entry_right->Key.key_str_len);
				return 1;
			}

			/*** TEST 3: The left's string is differ from right's ***/

			if (0 != strcmp(entry_left->Key.key_str, entry_right->Key.key_str)) {
				zentry_t *entry_tmp;
				DDD("Left->string key len not match Right->string key len : %s != %s ; in key : %lX <--> %lX\n",
					entry_left->Key.key_str,
					entry_right->Key.key_str,
					entry_left->Key.key_int64,
					entry_right->Key.key_int64);

				DD("Goint to search in left zhash by String Ket %s / len %u\n",
				   entry_left->Key.key_str,
				   entry_left->Key.key_str_len);

				/* Let's re-check that what we see is true */
				entry_tmp = zhash_entry_find_by_str(left, entry_left->Key.key_str, entry_left->Key.key_str_len);
				if (NULL == entry_tmp) {
					DE("Could not find entry by string: %s / len %u\n", entry_left->Key.key_str, entry_left->Key.key_str_len);
					abort();
				}

				DD("LEFT : Str Key: %s, Int Key: %lX\n", entry_tmp->Key.key_str, entry_tmp->Key.key_int64);

				DD("Goint to search in right zhash by String Key |%s| / len %u\n", entry_right->Key.key_str, entry_right->Key.key_str_len);

				entry_tmp = zhash_entry_find_by_str(right, entry_right->Key.key_str, entry_right->Key.key_str_len);
				if (NULL == entry_tmp) {
					DE("Could not find entry by string: %s / len %u\n", entry_right->Key.key_str, entry_right->Key.key_str_len);
					abort();
				}

				DD("RIGHT: Str Key: %s, Int Key: %lX\n", entry_tmp->Key.key_str, entry_tmp->Key.key_int64);

				return 1;
			}

			/*** TEST 4: The left's value size is differ from right's ***/

			if (entry_left->Val.val_size != entry_right->Val.val_size) {
				DDD("Left->val size not match Right->val size : %u != %u\n",
					entry_left->Val.val_size, entry_right->Val.val_size);
				return 1;
			}

			/*** TEST 5: The left's value is differ from right's ***/

			if (0 != memcmp(entry_left->Val.val, entry_right->Val.val,  entry_left->Val.val_size)) {
				DDD("Left->val not match Right->val\n");
				return 1;
			}

			entry_left = entry_left->next;
		}
	}

	return 0;
}

