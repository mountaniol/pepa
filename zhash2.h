#ifndef ZHASH_H
#define ZHASH_H

#include <stdbool.h>

/* hash table
 * keys are strings or integers
 * values are void *pointers */

#define COUNT_OF(arr) (sizeof(arr) / sizeof(*arr))
#define zfree free

/* struct representing an entry in the hash table */
struct ZHashEntry {
  /* If key is a string, the 'key' will be != NULL */
  char *key;
  /* If the key is integer, then key_int set and 'key' == NULL */
  u_int32_t key_int;
  void *val;
  struct ZHashEntry *next;
};

typedef struct ZHashEntry zentry_t;

/* struct representing the hash table
  size_index is an index into the hash_sizes array in hash.c */
struct ZHashTable {
  size_t size_index;
  size_t entry_count;
  zentry_t **entries;
};

typedef struct ZHashTable ztable_t;

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func ztable_t* zcreate_hash_table(void)
 * @brief Create new empty hash table
 * @param void
 * @return ztable_t* Pointer to new hash table on success, NULL on error
 * @details
 */
ztable_t *zcreate_hash_table(void);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void zfree_hash_table(ztable_t *hash_table)
 * @brief Release hash table.
 * @param ztable_t * hash_table Hash table to free
 * @details If the hash table is not empty, all entries will be released as well.
 *          The data kept in the entries not released
 */
void zfree_hash_table(ztable_t *hash_table);

/* hash operations */

/* Set of function wehere the key is a string */

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void zhash_insert_by_str(ztable_t *hash_table, char *key, void *val)
 * @brief Insert new entry into the hash table. The key is string.
 * @param ztable_t * hash_table A hash table to insert the new entry into
 * @param char * key The key to used for insert / search
 * @param void * val Pointer to data
 * @details
 */
void zhash_insert_by_str(ztable_t *hash_table, const char *key, void *val);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void* zhash_find_by_str(ztable_t *hash_table, char *key)
 * @brief Find an entry in hash table
 * @param ztable_t * hash_table The hash table to search
 * @param char * key A null terminated string to use as key for searching
 * @return void* Pointer to data kept in the hash table, NULL if not found
 * @details The found entry will be not released
 */
void *zhash_find_by_str(ztable_t *hash_table, char *key);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void* zhash_extract_by_str(ztable_t *hash_table, char *key)
 * @brief Find and extract data from the hash table using string as the search key
 * @param ztable_t * hash_table The hash table to search in
 * @param char * key A null terminated string to use for search
 * @return void* Data kept in hash table, NULL if not found
 * @details This function removes the found entry from the hash and returns data to caller
 */
void *zhash_extract_by_str(ztable_t *hash_table, char *key);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func bool zhash_exists_by_str(ztable_t *hash_table, char *key)
 * @brief Check if an entry for the given key exists in the hash table
 * @param ztable_t * hash_table The hash table to test
 * @param char * key A null terninated string to search
 * @return bool True if a record for the given key presens, false if doesnt
 * @details
 */
bool zhash_exists_by_str(ztable_t *hash_table, const char *key);

/* Set of function where the key is an integer */

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void zhash_insert_by_str(ztable_t *hash_table, char *key, void *val)
 * @brief Insert new entry into the hash table. The key is integer.
 * @param ztable_t * hash_table A hash table to insert the new entry into
 * @param u_int32_t key The key to use for insert / search
 * @param void * val Pointer to data
 * @details
 */
void zhash_insert_by_int(ztable_t *hash_table, u_int32_t key, void *val);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void *zhash_find_by_int(ztable_t *hash_table, u_int32_t key)
 * @brief Find an entry in hash table
 * @param ztable_t * hash_table The hash table to search
 * @param u_int32_t key An integer value to use as key for searching
 * @return void* A pointer to data kept in the hash table, NULL if not found
 * @details The found entry will be deleted from the table
 */
void *zhash_find_by_int(ztable_t *hash_table, u_int32_t key);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void *zhash_extract_by_int(ztable_t *hash_table, u_int32_t key)
 * @brief Find and extract data from the hash table using an integer as the search key
 * @param ztable_t * hash_table The hash table to search in
 * @param u_int32_t key An integer value to use for search
 * @return void* Data kept in hash table, NULL if not found
 * @details This function removes the found entry from the hash and returns data to caller
 */
void *zhash_extract_by_int(ztable_t *hash_table, u_int32_t key);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func bool zhash_exists_by_int(ztable_t *hash_table, u_int32_t key)
 * @brief Check if an entry for the given key exists in the hash table
 * @param ztable_t * hash_table The hash table to test
 * @param u_int32_t key An integer value to search
 * @return bool True if the a record for the given key presens, false if doesnt
 * @details
 */
bool zhash_exists_by_int(ztable_t *hash_table, u_int32_t key);

/* hash entry creation and destruction */

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func zentry_t* zentry_t_alloc_str(char *key, void *val)
 * @brief Allocate new entry for the hash table
 * @param char * key Null terminated string to use as the key
 * @param void * val Pointer to a date to keep in the hash table
 * @return zentry_t* Pointer to a new zenty_t structure on success, NULL on error
 * @details
 */
zentry_t *zentry_t_alloc_str(const char *key, void *val);
/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func zentry_t *zentry_t_alloc_int(u_int32_t key, void *val)
 * @brief Allocate new entry for the hash table
 * @param char * key An integer to use as the key
 * @param void * val Pointer to a date to keep in the hash table
 * @return zentry_t* Pointer to a new zenty_t structure on success, NULL on error
 * @details
 */
zentry_t *zentry_t_alloc_int(u_int32_t key, void *val);

/**
 * @author Sebastian Mountaniol (23/08/2020)
 * @func void zentry_t_release(zentry_t *entry, bool recursive)
 * @brief Release zentry
 * @param zentry_t * entry The entry to release
 * @param bool recursive If this == true, and the entry holds several nodes (i.e. linked list) release all of them
 * @details
 */
void zentry_t_release(zentry_t *entry, bool recursive);

#endif

