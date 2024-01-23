#ifndef _IN_READING_SOCKETS_H__
#define _IN_READING_SOCKETS_H__

void pepa_in_reading_sockets_close_all(pepa_core_t *core);
void pepa_in_reading_sockets_free(pepa_core_t *core);
void pepa_in_reading_sockets_allocate(pepa_core_t *core, const int num);
void pepa_in_reading_sockets_add(pepa_core_t *core, const int fd);
void pepa_in_reading_sockets_close_rm(pepa_core_t *core, const int fd);

#endif /* _IN_READING_SOCKETS_H__ */
