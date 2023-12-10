#ifndef _SEC_DEBUG_H_
#define _SEC_DEBUG_H_
/*@-skipposixheaders@*/
#include <stdio.h>
#include <time.h>
/*@=skipposixheaders@*/

#ifdef DDD
	#undef DDD
#endif
#ifdef DD
	#undef DD
#endif
#ifdef D
	#undef D
#endif
#ifdef DE
	#undef DE
#endif
#ifdef DDE
	#undef DDE
#endif
#ifdef DDD0
	#undef DDD0
#endif

#define _D_PRINT(fmt, ...) do{printf("%s +%d : ", __func__, __LINE__); printf(fmt, ##__VA_ARGS__); }while(0 == 1)
#define _D_PRINT_ERR(fmt, ...) do{fprintf(stderr, "%s +%d [ERR] : ", __func__, __LINE__); printf(fmt, ##__VA_ARGS__); }while(0 == 1)

#define D _D_PRINT
#define DD _D_PRINT

#define DSVAR(x) do{DD("%s = |%s|\n", #x, x);}while(0);
#define DIVAR(x) do{DD("%s = |%d|\n", #x, x);}while(0);

#ifdef DEBUG3
	#define DDD _D_PRINT
#else
/* Extra debug */
	#define DDD(x,...) do{}while(0)
#endif // DEBUG3

/* Debug print */
#define DE _D_PRINT_ERR

/* Extended (noisy) debug print */
#ifdef DERROR3
	#define DDE _D_PRINT_ERR
#else
/* Extra debug */
	#define DDE(x,...) do{}while(0)
#endif // DEBUG3
#define DE _D_PRINT_ERR

/* DDD0 always empty : use it to keep printings */
#define DDD0(x,...) do{}while(0)

#define D_TIME_START(x) clock_t x##_start = clock();
#define D_TIME_END(x) {clock_t x##_end = clock(); D("Line %d: time used %f\n", __LINE__, (((double)(x##_end - x##_start))/ CLOCKS_PER_SEC));}

#endif /* _SEC_DEBUG_H_ */
