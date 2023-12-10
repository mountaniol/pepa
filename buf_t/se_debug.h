#ifndef _SEC_DEBUG_H_
#define _SEC_DEBUG_H_
/*@-skipposixheaders@*/
#include <stdio.h>
#include <time.h>
/*@=skipposixheaders@*/
#define D_EMPTY_PRINT(x,...) do{}while(0)

/* Print a message */
#define _D_PRINT(fmt, ...) do{printf("%s +%d : " fmt , __func__, __LINE__, ##__VA_ARGS__); }while(0 == 1)

/* Print an error */
#define _D_PRINT_ERR(fmt, ...) do{fprintf(stderr, "%s +%d [ERR] : " fmt, __func__, __LINE__, ##__VA_ARGS__); }while(0 == 1)

/* Print one liner message with return */
#define _D_PRINT_LINE(fmt, ...) do{printf("%s +%d : " fmt "\n" , __func__, __LINE__, ##__VA_ARGS__); }while(0 == 1)

/* Print an one-line error with line return  */
#define _D_PRINT_ERR_LINE(fmt, ...) do{fprintf(stderr, "%s +%d [ERR] : " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); }while(0 == 1)

#define D D_EMPTY_PRINT
#define DD D_EMPTY_PRINT
#define DDD D_EMPTY_PRINT

#define DL D_EMPTY_PRINT
#define DDL D_EMPTY_PRINT
#define DDDL D_EMPTY_PRINT

#define DE D_EMPTY_PRINT
#define DDE D_EMPTY_PRINT
#define DDDE D_EMPTY_PRINT

#define EL D_EMPTY_PRINT
#define EEL D_EMPTY_PRINT
#define EEEL D_EMPTY_PRINT

/* Empty printings */

#define DE0 D_EMPTY_PRINT
#define DDE0 D_EMPTY_PRINT
#define DDDE0 D_EMPTY_PRINT

#define E0 D_EMPTY_PRINT
#define EE0 D_EMPTY_PRINT
#define EEE0 D_EMPTY_PRINT

#define EL0 D_EMPTY_PRINT
#define EEL0 D_EMPTY_PRINT
#define EEEL0 D_EMPTY_PRINT

#define D0 D_EMPTY_PRINT
#define DD0 D_EMPTY_PRINT
#define DDD0 D_EMPTY_PRINT

/* Enable DEBUG3 & DEBUG2  */
#ifdef DEBUG3
	#undef DERROR3
	#define DERROR3

	#undef DEBUG2
	#define DEBUG2

	#undef DDD
	#define DDD _D_PRINT

	#undef DDDL
	#define DDDL _D_PRINT_LINE
#endif

/* Enable DEBUG2 & DEBUG  */
#ifdef DEBUG2
	#undef DERROR2
	#define DERROR2

	#undef DEBUG
	#define DEBUG

	#undef DD
	#define DD _D_PRINT

	#undef DDL
	#define DDL _D_PRINT_LINE
#endif

/* Enable DEBUG  */
#ifdef DEBUG
	#undef DERROR
	#define DERROR

	#undef D
	#define D _D_PRINT

	#undef DL
	#define DL _D_PRINT_LINE
#endif

/* Enable DERROR3 & DERROR2 */
#ifdef DERROR3
	#undef DERROR2
	#define DERROR2

	#undef DDDE
	#define DDDE _D_PRINT_ERR

	#undef EEE
	#define EEE _D_PRINT_ERR

	#undef EEEL
	#define EEEL _D_PRINT_ERR_LINE
#endif

/* Enable DERROR2 & DERROR */
#ifdef DERROR2
	#undef DERROR
	#define DERROR

	#undef DDE
	#define DDE _D_PRINT_ERR

	#undef EE
	#define EE _D_PRINT_ERR

	#undef EEL
	#define EEL _D_PRINT_ERR_LINE
#endif

/* Enable DERROR */
#ifdef DERROR
	#undef DE

	#undef DE
	#define DE _D_PRINT_ERR

	#undef E
	#define E _D_PRINT_ERR

	#undef EL
	#define EL _D_PRINT_ERR_LINE
#endif

#define ENTRY() do{DDL("Entry to the function %s", __func__);} while(0)

/* This used to test and print time of execution in the same function; if you copy thism don't forget include <time.h> */
#define D_TIME_START(x) clock_t x##_start = clock();
#define D_TIME_END(x) {clock_t x##_end = clock(); D("Line %d: time used %f\n", __LINE__, (((double)(x##_end - x##_start))/ CLOCKS_PER_SEC));}
#define D_TIME_END_MES(x, mes) {clock_t x##_end = clock(); D("%s: Line %d: time used %f\n", mes, __LINE__, (((double)(x##_end - x##_start))/ CLOCKS_PER_SEC));}

/* Start time measurement */
#define DD_TIME_START(x) clock_t x##_start = clock()
/* Stop time measurement */
#define DD_TIME_END(x) clock_t x##_end = clock()
/* Calulate the time */
#define DD_TIME_RESULT(x) ((((double)(x##_end - x##_start))/ CLOCKS_PER_SEC));
/* Calculate percent of 'fraction' time in the 'whole' time of the running */
#define DD_PERCENT_OF(fraction, whole) ((int) ((fraction * 100) / whole))

#define DSVAR(x) do{DD("%s = |%s|\n", #x, x);}while(0);
#define DIVAR(x) do{DD("%s = |%d|\n", #x, x);}while(0);

#endif /* _SEC_DEBUG_H_ */
