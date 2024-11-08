#ifndef CHECKSUM_H_
#define CHECKSUM_H_

#include <stdlib.h>
#include "optimization.h"

/* Used for Murmur calculation */
#define ZHASH_MURMUR_SEED (17)
int8_t checksum_buf_configured(const char *buf_input, const size_t buf_input_size, void *output);

int8_t checksum_buf_to_128_bit(const char *buf_input, const size_t buf_input_size, void *output_128);

int8_t checksum_buf_to_64_bit(const char *buf_input, const size_t buf_input_size, void *output_64);

int8_t checksum_buf_to_32_bit(const char *buf_input, const size_t buf_input_size, void *output_32);

int8_t checksum_buf_to_16_bit(const char *buf_input, const size_t buf_input_size, void *output_16);

int8_t checksum_buf_to_8_bit(const char *buf_input, const size_t buf_input_size, void *output_8);
#endif /* CHECKSUM_H_ */
