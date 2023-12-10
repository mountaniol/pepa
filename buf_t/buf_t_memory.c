/*@-skipposixheaders@*/
#include <stdlib.h>
#include <string.h>
#include "se_tests.h"
#include "se_debug.h"
#include "buf_t.h"
/*@=skipposixheaders@*/

/*@null@*/ /*@only@*/void *zmalloc(size_t sz)
{
	return calloc(sz, 1);
}

/*@null@*/ /*@only@*/void *zmalloc_any(size_t asked, size_t *allocated)
{
	/*@only@*//*@in@*/void *ret = NULL;

	while (NULL == ret && asked > 0) {
		ret = calloc(asked, 1);
		if (NULL != ret) {
			*allocated = asked;
			return (ret);
		}
		asked /= 2;
	}
	return NULL;
}

int zfree_size(void *mem, size_t size)
{
	if (NULL == mem) {
		DE("Got NULL pointer");
		TRY_ABORT();
		return (-1);
	}

	memset(mem, 0, size);
	free(mem);
	return 0;
}

