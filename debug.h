#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <assert.h>
/* Here are definition of debug prints:
 *
 * DDD - Noisy debug
 * DD - Informative debug
 * D - Minimal debug
 * DE - Errors debug
 * 
 * Open it with:
 * DDD - with DEBUG3
 * DD - with DEBUG2
 * D - with DEBUG1
 * DE - with DEBUGERRORS
 */

// #ifdef DSYSLOG


#define DE(...) do{}while(0)
#define D(...) do{}while(0)
#define DD(...) do{}while(0)
#define DDD(...) do{}while(0)

#   ifdef DSYSLOG
#   define _DDD_LSERVER_PRINT_OK(fmt, ...) do{syslog(LOG_NOTICE, "OK: %s +%d : ", __func__, __LINE__); printf(fmt, ##__VA_ARGS__); }while(0)
#   define _DDD_LSERVER_PRINT_ERR(fmt, ...) do{syslog(LOG_NOTICE, "ER: %s +%d : ", __func__, __LINE__); printf(fmt, ##__VA_ARGS__); }while(0)
//#   define _DDD_LSERVER_PRINT(fmt, ...) do{syslog(LOG_NOTICE, "%s +%d : %s", __func__, __LINE__, fmt, ##__VA_ARGS__); }while(0)
#   else
 #   define _DDD_LSERVER_PRINT_OK(fmt, ...) do{printf("OK: %s +%d : ", __func__, __LINE__); printf(fmt, ##__VA_ARGS__); }while(0)
 #   define _DDD_LSERVER_PRINT_ERR(fmt, ...) do{printf("ER: %s +%d : ", __func__, __LINE__); printf(fmt, ##__VA_ARGS__); }while(0)
//#   define _DDD_LSERVER_PRINT(fmt, ...) do{printf("%s +%d : %s", __func__, __LINE__, fmt, ##__VA_ARGS__); }while(0)
#   endif /* DSYSLOG */


#if defined(DEBUG3)
#   define DEBUGERRORS
#   define DEBUG2
#   define DEBUG1
#   undef DDD
#   define DDD _DDD_LSERVER_PRINT_OK
#endif /* DEBUG3 */

#if defined(DEBUG2)
#   define DEBUGERRORS
#   define DEBUG1
#   undef DD
#   define DD _DDD_LSERVER_PRINT_OK
#endif /* DEBUG*/

#if defined(DEBUG1)
#   define DEBUGERRORS
#   undef D
#   define D _DDD_LSERVER_PRINT_OK
#endif /* DEBUG*/

#if defined(DEBUGERRORS)
#   undef DE
#   define DE _DDD_LSERVER_PRINT_ERR
#endif /* DEBUG*/

#define TESTP_MES(x, ret, mes) do {if(NULL == x) { DE("%s\n", mes); return ret; } } while(0)
#define TESTP_VOID_MES(x, mes) do {if(NULL == x) { DE("%s\n", mes); return; } } while(0)
#define TESTP(x, ret) do {if(NULL == x) { DE("Pointer %s is NULL\n", #x); return ret; }} while(0)
#define TESTP_VOID(x) do {if(NULL == x) { DE("Pointer %s is NULL\n", #x); return; }} while(0)
#define TESTP_ASSERT(x) do {if	(NULL == x) { DE("Pointer %s is NULL\n", #x); assert(x != NULL); }} while(0)
#define TESTP_ASSERT_MES(x, mes) do {if	(NULL == x) { DE("Pointer %s is NULL: [%s]\n", #x, mes); assert(x != NULL); }} while(0)
#define TESTP_GO(x, lable) do {if(NULL == x) { DE("Pointer %s is NULL\n", #x); goto lable; } } while(0)
#define TFREE_SIZE(x,sz) do { if(NULL != x) {memset(x,0,sz);free(x); x = NULL;} else {DE(">>>>>>>> Tried to free_size() NULL: %s (%s +%d)\n", #x, __func__, __LINE__);} }while(0)

#endif /* _DEBUG_H_ */
