#ifndef _IN_READING_SOCKETS_H__
#define _IN_READING_SOCKETS_H__

/* Used internally in IN reading sockets array */
#define EMPTY_SLOT (-1)

void pepa_in_reading_sockets_close_all(pepa_core_t *core);
void pepa_in_reading_sockets_free(pepa_core_t *core);
void pepa_in_reading_sockets_allocate(pepa_core_t *core, const int num);
void pepa_in_reading_sockets_add(pepa_core_t *core, const int fd);
int pepa_in_if_socket_is_in(pepa_core_t *core, const int fd);
int pepa_in_readers_number(pepa_core_t *core);
void pepa_in_reading_sockets_close_rm(pepa_core_t *core, const int fd);

#endif /* _IN_READING_SOCKETS_H__ */
