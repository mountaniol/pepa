#include "buf_t.h"
#include "buf_t_memory.h"
#include "buf_t_debug.h"

/* This file implements buf_t statictics */

/* Buffers statistics */

/* buf_t statisctics */
float  average_buf_size    = 0;         /* Average allocation */
size_t buf_allocs_num      = 0;          /* Number of buf_t structures allocations */
size_t buf_release_num     = 0;         /* Number of freed  buf_t structures */
size_t buf_regular_num     = 0;         /* How many regular bufs allocated (with buf_new) */
size_t buf_string_num      = 0;          /* How many string bufs allocated (with buf_string) */
size_t buf_ro_num          = 0;              /* How many read-only bufs existed (with buf_string) */


/* buf->data statistics */
size_t max_data_size       = 0;           /* The biggest allocated buffer */
size_t data_allocated      = 0;          /* How many bytes of data allocated in total */
size_t data_released       = 0;           /* How many bytes of data relesed in total */
/* If (data_allocated - data_released) > 0 we have a memory leak */

size_t buf_realloc_calls   = 0;       /* How many time realloc called during the app life */
size_t buf_realloc_max     = 0;         /* Max size of realloc */
float  buf_realloc_average = 0;      /* Average size of realloc */


#if 0
void average_buf_size_inc(size_t buf_size){
	/* TODO */
}
#endif

void buf_allocs_num_inc()
{
	buf_allocs_num++;
}

void buf_release_num_inc()
{
	buf_release_num++;
}

void buf_regular_num_inc()
{
	buf_regular_num++;
}

void buf_string_num_ins()
{
	buf_string_num++;
}

void buf_ro_num_inc()
{
	buf_ro_num++;
}

void max_data_size_upd(size_t data_size)
{
	if (max_data_size < data_size) {
		max_data_size = data_size;
	}
}

void data_allocated_inc(size_t allocated)
{
	data_allocated += allocated;
}

void data_released_inc(size_t released)
{
	data_released += released;
}

void buf_realloc_calls_inc()
{
	buf_realloc_calls++;
}

void buf_realloc_max_upd(size_t realloced)
{
	if (buf_realloc_max < realloced) {
		buf_realloc_max = realloced;
	}
}

#if 0
void buf_realloc_average_upd(size_t realloced){
	/* TODO */
}
#endif


void buf_t_stats_print()
{
	printf("Average buf allocation size: %f\n", average_buf_size);
	/*@ignore@*/
	printf("Number of buf_t allocations: %zu\n", buf_allocs_num);
	printf("Number of buf_t releases:    %zu\n", buf_release_num);
	/*@end@*/

	//printf("Number of buf_t releases:    %zu\n", buf_release_num);
}