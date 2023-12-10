#ifndef _SEBLIB_COMMON_H_
#define _SEBLIB_COMMON_H_

/*
 * In this include file defined different tests.
 * Here's the list with short description.
 *
 * TESTP_MES(x, ret, mes) : Test pointer 'x'. If it is NULL, print 'mes' and return 'ret'
 * TESTP(x, ret) : Test pointer 'x'. If it is the NULL pointer, debug print 'Pointer %s is NULL' where %s is name of the variable. Return 'ret'
 * TODO: Complete it
 *
 *
 */

/*@-skipposixheaders@*/
#include <stdlib.h>
#include <assert.h>
#include <string.h>
/*@=skipposixheaders@*/

/* Testing macros. Part 1: Test and Return */
/* Test pointer for NULL. If NULL, print "mes" and return "ret" */
#define TESTP_MES(x, ret, mes) do {if(NULL == x) { DE("%s\n", mes); return ret; } } while(0) 

/* Shorter form of the tester: print message "Pointer x is NULL", x replaced with argument x name */
#define TESTP(x, ret) do {if(NULL == x) { DDE("Pointer %s is NULL\n", #x); return ret; }} while(0)

#define TESTP_VOID(x) do {if(NULL == x) { DDE("Pointer %s is NULL\n", #x); return; }} while(0)

/* Test if x == 0 . If x != 0, print "mes" and return "ret" */
#define TESTI_MES(x, ret, mes) do {if(0 != x) { DE("%s\n", mes); return ret; } } while(0)

/* Shorter form of the tester: print message "Pointer x is NULL", x replaced with argument x name */
#define TESTI(x, ret) do {if(0 != x) { DE("Pointer %s is NULL\n", #x); return ret; } } while(0)

/* Test if x == 0 . If x != 0, print "mes" and goto "lable" */
#define  TESTI_GO(x, lable) do {if(0 != x) { DE("Pointer %s is NULL\n", #x); goto lable; } } while(0)

/* Testing macros. Part 1: Test and goto */
/* Test pointer for NULL. If NULL, print "mes" and goto "lable" */
#define TESTP_MES_GO(x, lable, mes) do {if(NULL == x) { DE("%s\n", mes); goto lable; } } while(0)

/* Shorter form of the tester: print message "Pointer x is NULL", x replaced with argument x name */
#define TESTP_GO(x, lable) do {if(NULL == x) { DE("Pointer %s is NULL\n", #x); goto lable; } } while(0)

/* Test if x == 0 . If x != 0, print "mes" and goto "lable" */
#define TESTI_MES_GO(x, lable, mes) do {if(0 != x) { DE("%s\n", mes); goto lable; } } while(0)

/* Print variable name and variable string*/
#define PRINTP_STR(p) do {DD("Pointer %s is %s\n", #p, p);}while(0)

#define TESTP_ASSERT(x, mes) do {if(NULL == x) { DE("[[ ASSERT! ]] %s == NULL: %s\n", #x, mes); abort(); } } while(0)
#define TESTI_ASSERT(var, mes) do {if(0 != var) { DE("[[ ASSERT! ]] %s != 0: %s\n", #var, mes); abort(); } } while(0)

#define TFREE(x) do { if(NULL != x) {free(x); x = NULL;} else {DE(">>>>>>>> Tried to free() NULL: %s\n", #x);} }while(0)
/* Secure version of free: memset emory to 0 before it freed */
#define TFREE_SIZE(x,sz) do { if(NULL != x) {memset(x,0,sz);free(x); x = NULL;} else {DE(">>>>>>>> Tried to free_size() NULL: %s (%s +%d)\n", #x, __func__, __LINE__);} }while(0)
/* Secure version of string free: set string memory to 0 before release it */
#define TFREE_STR(x) do { if(NULL != x) {memset(x,0,strlen(x));free(x); x = NULL;} else {DE(">>>>>>>> Tried to free_size() NULL: %s\n", #x);} }while(0)

#endif /* _SEBLIB_COMMON_H_ */

