#ifndef BUF_T_STATS_H
#define BUF_T_STATS_H

/* Define statistics entries */
#define BUF_T_STAT_BUF_ADD  (0)
#define BUF_T_STAT_BUF_FREE (1)

/* This structure keeps per function statistics */
struct buf_t_stat_data
{
	size_t calls; /* How many times this function called */
	size_t eok; /* How many times this function finished with EOK status */
	/* Note: The (calls - ok) = number of errors */
	/* Note: for functions like buf_new, buf_string this number also number of allocated bufs of this
	   type */
	size_t einval; /* How many times the function terminated because invalid argument */
	unsigned int max_time; /* Max running time of this function  */
	float average_time; /* Average time of the function run */
};

/* Here's definition of traced functions */
enum e_func {
	buf_set_canary_e = 0,
	buf_force_canary_e,
	buf_test_canary_e,
	buf_get_canary_e,
	buf_is_valid_e,
	buf_new_e,
	buf_string_e,
	buf_from_string_e,
	buf_set_data_e,
	buf_set_data_ro_e,
	buf_steal_data_e,
	buf_2_data_e,
	buf_add_room_e,
	buf_test_room_e,
	buf_clean_e,
	buf_free_e,
	buf_add_e,
	buf_used_e,
	buf_room_e,
	buf_pack_e,
	buf_detect_used_e,
	buf_sprintf_e,
	buf_recv_e,

	buf_last_e
};

#define FUNC_STAT_ENTRY __func__##"e"


void average_buf_size_inc(size_t buf_size);
void buf_allocs_num_inc(void);
void buf_release_num_inc(void);
void buf_regular_num_inc(void);
void buf_string_num_ins(void);
void buf_ro_num_inc();
void max_data_size_upd(size_t data_size);
void data_allocated_inc(size_t allocated);
void data_released_inc(size_t released);
void buf_realloc_calls_inc(void);
void buf_realloc_max_upd(size_t realloced);
void buf_realloc_average_upd(size_t realloced);
void buf_t_stats_print(void);
#endif /* BUF_T_STATS_H */
