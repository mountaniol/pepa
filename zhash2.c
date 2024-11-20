#include <string.h>
#include <stdlib.h>
#include "zhash2.h"

/* helper functions */
static size_t next_size_index(size_t size_index);
static size_t previous_size_index(size_t size_index);
static ztable_t *zcreate_hash_table_with_size(size_t size_index);
static void *zmalloc(size_t size);
static void *zcalloc(size_t num, size_t size);

/* possible sizes for hash table; must be prime numbers */
static const size_t hash_sizes[] = {
	53, 101, 211, 503, 1553, 3407, 6803, 12503, 25013, 50261,
	104729, 250007, 500009, 1000003, 2000029, 4000037, 10000019,
	25000009, 50000047, 104395301, 217645177, 512927357, 1000000007
};

ztable_t *zcreate_hash_table(void)
{
	return (zcreate_hash_table_with_size(0));
}

static ztable_t *zcreate_hash_table_with_size(size_t size_index)
{
	ztable_t *hash_table;

	hash_table = zmalloc(sizeof(ztable_t));

	hash_table->size_index = size_index;
	hash_table->entry_count = 0;
	hash_table->entries = zcalloc(hash_sizes[size_index] , sizeof(void *));

	return (hash_table);
}

static size_t zgenerate_hash_by_str(const ztable_t *hash_table, const char *key)
{
	size_t size;
	size_t hash;
	char   ch;

	size = hash_sizes[hash_table->size_index];
	hash = 0;

	while ((ch = *key++)) hash = (17 * hash + ch) % size;

	return (hash);
}

static size_t zgenerate_hash_by_int(const ztable_t *hash_table, u_int32_t key)
{
	size_t size;
	size_t hash;

	size = hash_sizes[hash_table->size_index];
	hash = 0;

	hash = key % size;

	return (hash);
}

static void zhash_rehash(ztable_t *hash_table, size_t size_index)
{
	size_t   hash;
	size_t   size;
	size_t   ii;
	zentry_t **entries;

	if (size_index == hash_table->size_index) return;

	size = hash_sizes[hash_table->size_index];
	entries = hash_table->entries;

	hash_table->size_index = size_index;
	hash_table->entries = zcalloc(hash_sizes[size_index] , sizeof(void *));

	for (ii = 0; ii < size; ii++) {
		zentry_t *entry;

		entry = entries[ii];
		while (entry) {
			zentry_t *next_entry;

			if (entry->key) {
				hash = zgenerate_hash_by_str(hash_table, entry->key);
			} else {
				hash = zgenerate_hash_by_int(hash_table, entry->key_int);
			}
			next_entry = entry->next;
			entry->next = hash_table->entries[hash];
			hash_table->entries[hash] = entry;

			entry = next_entry;
		}
	}

	zfree(entries);
}

void zfree_hash_table(ztable_t *hash_table)
{
	size_t size; 
	size_t ii;

	size = hash_sizes[hash_table->size_index];

	for (ii = 0; ii < size; ii++) {
		zentry_t *entry;

		if ((entry = hash_table->entries[ii] )) zentry_t_release(entry, true);
	}

	zfree(hash_table->entries);
	zfree(hash_table);
}

void zhash_insert_by_str(ztable_t *hash_table, const char *key, void *val)
{
	size_t   size;
	size_t   hash;
	zentry_t *entry;

	hash = zgenerate_hash_by_str(hash_table, key);
	entry = hash_table->entries[hash];

	while (entry) {
		if (strcmp(key, entry->key) == 0) {
			entry->val = val;
			return;
		}
		entry = entry->next;
	}

	entry = zentry_t_alloc_str(key, val);

	entry->next = hash_table->entries[hash];
	hash_table->entries[hash] = entry;
	hash_table->entry_count++;

	size = hash_sizes[hash_table->size_index];

	if (hash_table->entry_count > size / 2) {
		zhash_rehash(hash_table, next_size_index(hash_table->size_index));
	}
}

void zhash_insert_by_int(ztable_t *hash_table, u_int32_t key, void *val)
{
	size_t   size;
	size_t   hash;
	zentry_t *entry;

	hash = zgenerate_hash_by_int(hash_table, key);
	entry = hash_table->entries[hash];

	while (entry) {
		if (key == entry->key_int) {
			entry->val = val;
			return;
		}
		entry = entry->next;
	}

	entry = zentry_t_alloc_int(key, val);

	entry->next = hash_table->entries[hash];
	hash_table->entries[hash] = entry;
	hash_table->entry_count++;

	size = hash_sizes[hash_table->size_index];

	if (hash_table->entry_count > size / 2) {
		zhash_rehash(hash_table, next_size_index(hash_table->size_index));
	}
}

void *zhash_find_by_str(ztable_t *hash_table, const char *key)
{
	size_t   hash;
	zentry_t *entry;

	hash = zgenerate_hash_by_str(hash_table, key);
	entry = hash_table->entries[hash];

	while (entry && strcmp(key, entry->key) != 0) entry = entry->next;

	return (entry ? entry->val : NULL);
}

void *zhash_find_by_int(ztable_t *hash_table, u_int32_t key)
{
	size_t   hash;
	zentry_t *entry;

	hash = zgenerate_hash_by_int(hash_table, key);
	entry = hash_table->entries[hash];

	while (entry && (key != entry->key_int)) entry = entry->next;

	return (entry ? entry->val : NULL);
}

void *zhash_extract_by_str(ztable_t *hash_table, const char *key)
{
	size_t   size;
	size_t   hash;
	zentry_t *entry;
	void     *val;

	hash = zgenerate_hash_by_str(hash_table, key);
	entry = hash_table->entries[hash];

	if (entry && strcmp(key, entry->key) == 0) {
		hash_table->entries[hash] = entry->next;
	} else {
		while (entry) {
			if (entry->next && strcmp(key, entry->next->key) == 0) {
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

	val = entry->val;
	zentry_t_release(entry, false);
	hash_table->entry_count--;

	size = hash_sizes[hash_table->size_index];

	if (hash_table->entry_count < size / 8) {
		zhash_rehash(hash_table, previous_size_index(hash_table->size_index));
	}

	return (val);
}

void *zhash_extract_by_int(ztable_t *hash_table, u_int32_t key)
{
	size_t   size;
	size_t   hash;
	zentry_t *entry;
	void     *val;

	hash = zgenerate_hash_by_int(hash_table, key);
	entry = hash_table->entries[hash];

	if (entry && key == entry->key_int) {
		hash_table->entries[hash] = entry->next;
	} else {
		while (entry) {
			if (entry->next && (key == entry->next->key_int)) {
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

	val = entry->val;
	zentry_t_release(entry, false);
	hash_table->entry_count--;

	size = hash_sizes[hash_table->size_index];

	if (hash_table->entry_count < size / 8) {
		zhash_rehash(hash_table, previous_size_index(hash_table->size_index));
	}

	return (val);
}

bool zhash_exists_by_str(ztable_t *hash_table, const char *key)
{
	size_t   hash;
	zentry_t *entry;

	hash = zgenerate_hash_by_str(hash_table, key);
	entry = hash_table->entries[hash];

	while (entry && strcmp(key, entry->key) != 0) entry = entry->next;

	return (entry ? true : false);
}

bool zhash_exists_by_int(ztable_t *hash_table, u_int32_t key)
{
	size_t   hash;
	zentry_t *entry;

	hash = zgenerate_hash_by_int(hash_table, key);
	entry = hash_table->entries[hash];

	while (entry && key != entry->key_int) entry = entry->next;
	if (entry) {
		return true;
	}

	return false;
}

zentry_t *zentry_t_alloc_str(const char *key, void *val)
{
	zentry_t *entry;
	char     *key_cpy;

	key_cpy = zmalloc((strlen(key) + 1) * sizeof(char));
	entry = zmalloc(sizeof(zentry_t));

	strcpy(key_cpy, key);
	entry->key = key_cpy;
	entry->val = val;

	return (entry);
}

zentry_t *zentry_t_alloc_int(u_int32_t key, void *val)
{
	zentry_t *entry;

	entry = zmalloc(sizeof(zentry_t));

	entry->key_int = key;
	entry->val = val;

	return (entry);
}

void zentry_t_release(zentry_t *entry, bool recursive)
{
	if (recursive && entry->next) zentry_t_release(entry->next, recursive);

	if (NULL != entry->key) {
		zfree(entry->key);
	}

	zfree(entry);
}

static size_t next_size_index(size_t size_index)
{
	if (size_index == COUNT_OF(hash_sizes)) return (size_index);

	return (size_index + 1);
}

static size_t previous_size_index(size_t size_index)
{
	if (size_index == 0) return (size_index);

	return (size_index - 1);
}

static void *zmalloc(size_t size)
{
	void *ptr;

	ptr = malloc(size);

	if (!ptr) exit(EXIT_FAILURE);

	memset(ptr, 0, size);
	return (ptr);
}

static void *zcalloc(size_t num, size_t size)
{
	void *ptr;

	ptr = calloc(num, size);

	if (!ptr) exit(EXIT_FAILURE);

	return (ptr);
}