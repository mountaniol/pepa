#ifndef _PEPA_ERRORS_H__
#define _PEPA_ERRORS_H__

#include "pepa_types.h"

__attribute__((warn_unused_result))
/**
 * @author Sebastian Mountaniol (12/7/23)
 * @brief Convert PEPA internal error code to string describing
 *  	  the code
 * @param int code  The code to convert to string
 * @return const char* Constant string describing the error code
 * @details The code can be negative or positive, the absolute
 *  		value will be used.
 * The returned string is constant and should not be freed().
 */
const char *pepa_error_code_to_str(int32_t code);
#endif /* _PEPA_ERRORS_H__ */
