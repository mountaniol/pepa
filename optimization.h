#ifndef OPTIMIZATION_H_
#define OPTIMIZATION_H_

/* GCC attributes */
/*
 * This file contains "friendly looking" macros of GCC attributes.
 * All these attributes are related to function.
 * Here is a short explanation:
 * FATTR_HOT             - This is a frequently used function
 * FATTR_COLD            - This is a rarely called function
 * FATTR_NONULL(X,Y,Z)   - Specify that the function arguments (X,Y,Z) are not NULL pointers
 * FATTR_NONULL()        - Specify that all function arguments are not NULL pointer
 * FATTR_PURE            - This function does not change the program's global state,
 * 							no external memory write, and its output purely depends on the input
 * FATTR_CONST           - This function is not ever read external memory,
 * 							and its output depends on its input arguments only
 * FATTR_UNUSED          - This function can be unused. It is fine, don't warn me
 * FATTR_WARN_UNUSED_RET - The return value of this function MUST be used. Warn me if it is not.
 *
 * How to use it:
 * Use it as a prefix of a function.
 * You can use more than one prefix and set it on multiple lines:
 *
 * FATTR_WARN_UNUSED_RET
 * FATTR_HOT FATTR_PURE
 * FATTR_NONULL(1,2)
 * int test_strings(const char *str1, const char *str2) {
 *     ...
 *     return 0;
 * }
 *
 * The function above: Frequently used (HOT) , does not change external memory (PURE),
 * for the same input buffers it returns the same predictable result (the change of buffers is traced automatically) (PURE),
 * both arguments are not NULL pointers (NONULL(1,2)),
 * and the returned value of this function must be used (at least tested) (WARN_UNUSED_RET).
 */

/* This function is hot, used intensively */
#define FATTR_HOT __attribute__((hot))
/* This function is cold, not expected to be used intensively */
#define FATTR_COLD __attribute__((cold))

/*
 * Specify those function arguments at position (1,2,..) of the function expected to be not NULL.
 * You  can use it without arguments:
 * ATTR_NONULL() void *box_steal_data(basket_t *basket, const box_u32_t box_num) *
 * Or with arguments:
 * ATTR_NONULL(1) void *box_steal_data(basket_t *basket, const box_u32_t box_num)
 */
#define FATTR_NONULL(...) __attribute__((nonnull(__VA_ARGS__)))

/* This function never returns a NULL pointer */
#define FATTR_RET_NONULL __attribute__((returns_nonnull))

/* A 'pure' function is a function that does not affect the global state of the program.
 * It means: no external memory is changed (no writes).
 * A 'pure' function for every input returns the same output.
 * The OS will cash the 'pure' function results and execute no calculation for the cashed inputs.
 * See more about it here. Also, it covers the difference between 'pure' and 'const' functions:
 * https://stackoverflow.com/questions/29117836/attribute-const-vs-attribute-pure-in-gnu-c
 */
#define FATTR_PURE __attribute__((pure))

/*
 * A 'const' function is like 'pure'.
 * It must not access (read or write) the external memory.
 * A 'const' function must return a value based only on parameters.
 * A 'const' function can not be 'void.'
 * Results of the 'const' function are cashed for performance improvement.
 */
#define FATTR_CONST __attribute__((const))

/* This function can be unused, and it is OK. Don't print a warning on the compilation. */
#define FATTR_UNUSED __attribute__((unused))

/*
 * The return value of this function must be used.
 * If the return value is unused, the compiler will warn about it.
 */
#define FATTR_WARN_UNUSED_RET __attribute__((warn_unused_result))

#endif /* OPTIMIZATION_H_ */
