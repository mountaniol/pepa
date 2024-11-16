#ifndef _IN_READING_SOCKETS_H__
#define _IN_READING_SOCKETS_H__

/* Used internally in IN reading sockets array */
#define EMPTY_SLOT (-1)

#define PROCESSONG_ON (1)
#define PROCESSONG_OFF (0)

#define FD_IS_IN (1)
#define FD_NOT_IN (0)

void set_reader_processing_on(pepa_core_t *core, const int number);
void set_reader_processing_off(pepa_core_t *core, const int number);
int test_reader_processing_on(pepa_core_t *core, const int number);

void pepa_in_reading_sockets_close_all(pepa_core_t *core);
void pepa_in_reading_sockets_free(pepa_core_t *core);
void pepa_in_reading_sockets_allocate(pepa_core_t *core, const int num);
void pepa_in_reading_sockets_add(pepa_core_t *core, const int fd);
void pepa_in_reading_sockets_close_rm(pepa_core_t *core, const int fd);
int pepa_in_find_slot_by_fd(pepa_core_t *core, const int fd);
int pepa_if_fd_in(pepa_core_t *core, const int fd);

#endif /* _IN_READING_SOCKETS_H__ */
