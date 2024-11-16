#include "checksum.h"
#include "murmur3.h"
//#include "tests.h"
#include "debug.h"
//#include "fnv/fnv.h"

#define CHECKSUM_32_BITS (1)
/**
 * Regarding hashing algorythm:
 * See https://theses.liacs.nl/pdf/2014-2015NickvandenBosch.pdf
 * From this documentyou can see that Fnv1a shows the best
 * performance and low collision rate.
 */

int8_t checksum_buf_to_128_bit(const char *buf_input, const size_t buf_input_size, void *output_128)
{
	TESTP(buf_input, -1);
	TESTP(output_128, -1);
	if (0 == buf_input_size) {
		DE("Size of the input buffer == 0\n");
		return -1;
	}

	MurmurHash3_x86_128(buf_input, buf_input_size, ZHASH_MURMUR_SEED, output_128);
	return 0;
}

int8_t checksum_buf_to_64_bit(const char *buf_input, const size_t buf_input_size, void *output_64)
{
	// uint64_t result;
	TESTP(buf_input, -1);
	TESTP(output_64, -1);
	if (0 == buf_input_size) {
		DE("Size of the input buffer == 0\n");
		return -1;
	}

	MurmurHash3_x86_128_to_64(buf_input, buf_input_size, ZHASH_MURMUR_SEED, output_64);
	//result = fnv_64a_buf(buf_input, buf_input_size, FNV1_64_INIT);
	// *((uint64_t *)output_64) = result;
	return 0;
}

int8_t checksum_buf_to_32_bit(const char *buf_input, const size_t buf_input_size, void *output_32)
{
	// uint32_t result;
	TESTP(buf_input, -1);
	TESTP(output_32, -1);
	if (0 == buf_input_size) {
		DE("Size of the input buffer == 0\n");
		return -1;
	}

	DDD("Going to calculate checksum: buf %p, size %zu, output %p\n", buf_input, buf_input_size, output_32);
	MurmurHash3_x86_32(buf_input, buf_input_size, ZHASH_MURMUR_SEED, output_32);
	// SuperFastHash(buf_input, buf_input_size);
	//result = fnv_32a_buf(buf_input, buf_input_size, FNV1_32_INIT);
	// *((uint32_t *)output_32) = result;
	DDD("Calculated checksum: %X\n", *((uint32_t *)output_32));
	return 0;
}

int8_t checksum_buf_to_16_bit(const char *buf_input, const size_t buf_input_size, void *output_16)
{
	uint32_t result;
	uint32_t output_32;
	const uint16_t *output_16_p = (uint16_t *)&output_32;
	result = checksum_buf_to_32_bit(buf_input, buf_input_size, &output_32);

	if (result) {
		return result;
	}

	*((uint16_t *)output_16) = output_16_p[0] ^ output_16_p[1];
	return 0;
}

int8_t checksum_buf_to_8_bit(const char *buf_input, const size_t buf_input_size, void *output_8)
{
	uint32_t result;
	uint32_t output_32;
	const uint8_t  *output_8_p = (uint8_t  *)&output_32;
	result = checksum_buf_to_32_bit(buf_input, buf_input_size, &output_32);

	if (result) {
		return result;
	}

	*((uint8_t *)output_8) = output_8_p[0] ^ output_8_p[1] ^ output_8_p[2] ^ output_8_p[3];
	return 0;
}

int8_t checksum_buf_configured(const char *buf_input, const size_t buf_input_size, void *output)
{
#if defined (CHECKSUM_8_BITS)
	return checksum_buf_to_8_bit(buf_input, buf_input_size, output);
#elif defined (CHECKSUM_16_BITS)
	return checksum_buf_to_16_bit(buf_input, buf_input_size, output);
#elif defined (CHECKSUM_32_BITS)
	return checksum_buf_to_32_bit(buf_input, buf_input_size, output);
#elif defined (CHECKSUM_64_BITS)
	return checksum_buf_to_64_bit(buf_input, buf_input_size, output);
#else
	#error You must define size of checksum_t type
	#error Use one of defines: CHECKSUM_8_BITS / CHECKSUM_16_BITS / CHECKSUM_32_BITS / CHECKSUM_64_BITS
#endif /* TICKET_8/16/32/64_BITS */
}

