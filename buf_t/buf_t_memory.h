#ifndef _SEC_MEMORY_H_
#define _SEC_MEMORY_H_
/*@-skipposixheaders@*/
/* For size_t */
#include <stddef.h>
/*@=skipposixheaders@*/

/**
 * @author Sebastian Mountaniol (22/05/2020)
 * @func void* zmalloc(size_t sz)
 * @brief Allocates buffer of 'sz' size. If allocated, clean the
 *  	  buffer (fills with zeroes).
 * @param size_t sz 
 * 
 * @return void* Pointer to new buffer on success, NULL on 
 *  	   failure
 * @details 
 */
extern /*@null@*/ /*@only@*/ void *zmalloc(size_t sz);

/**
 * @author Sebastian Mountaniol (22/05/2020)
 * @func void* zmalloc_any(size_t asked, size_t *allocated)
 * @brief Returns buffer of 'asked' bytes or less. In 
 *  	  'allocated' returns size of allocated buffer.
 * 
 * @param size_t asked How many bytes asked to allocate
 * @param size_t* allocated How many bytes really allocated
 * 
 * @return void* Pointer to allocated buffer. NULL on failure.
 * @details If allocation of 'asked' number of bytes failed, the 
 *  		function divide the 'asked' by 2 and try again until
 *  		'asked' > 0
 */
extern /*@null@*/ /*@only@*/ void *zmalloc_any(size_t asked, size_t *allocated);

/**
 * @author Sebastian Mountaniol (11/20/23)
 * @brief Feel memory with 0 and then free the memory
 * @param void* mem   Memory to free
 * @param size_t size  Size of the memory area
 * @return int return 0 on success, -1 in case ULL pointer
 *  	   received
 * @details This function make hacker's life harder :)
 */
extern int zfree_size(void *mem, size_t size);
#endif /* _SEC_MEMORY_H_ */
