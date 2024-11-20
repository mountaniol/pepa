#ifndef PEPA_TICKET_ID__
#define PEPA_TICKET_ID__

typedef uint32_t pepa_ticket_t;
typedef uint32_t pepa_id_t;

typedef struct {
    pepa_ticket_t ticket; /**< The ticket generated */
    uint32_t pepa_len; /**< The length of this buffer send from SHVA */
    pepa_ticket_t pepa_id; /**< The pepa ID */
} __attribute__((packed)) pepa_prebuf_t;

    pepa_ticket_t pepa_gen_ticket(pepa_ticket_t seed);

#endif /*  PEPA_TICKET_ID__ */
    