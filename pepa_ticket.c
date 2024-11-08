#include <stdint.h>
#include "pepa_ticket_id.h"

/**
 * @author se (9/24/24)
 * @brief This function generate a pseudorandom number. This number is not really random but we do not
 *  	  needreal randomness. We need to generate a unique number for every next buffer. This function is a
 *  	  very fast and efficient one instead.
 * @param seed   A previous value of the pseudorandom number
 * @return unsigned int The next pseudorandom number
 * @details The ticket is never == 0
 */
#if 0 /* SEB */ /* 01/11/2024 */
pepa_ticket_t pepa_gen_ticket(pepa_ticket_t seed){
    pepa_ticket_t ticket = (214013 * seed + 2531011);

    if (0 == ticket) {
        ticket = seed;
    }
    return ticket;
}
#endif /* SEB */ /* 01/11/2024 */

pepa_ticket_t pepa_gen_ticket(pepa_ticket_t seed)
{
    // Constants for the LCG algorithm (these can vary, here are common values)
    pepa_ticket_t ret;
    const uint32_t a = 1664525;
    const uint32_t c = 1013904223;

    // Update the seed using the LCG formula
    ret = a * (seed) + c;

    // Return the generated number
    return ret;
}
