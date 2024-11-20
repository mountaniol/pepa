#ifndef MACROS_H__
#define MACROS_H__

#define TFREE(x) do { if(NULL != x) {free(x); x = NULL;} else {slog_error_l(">>>>>>>> Tried to free() NULL: %s\n", #x);} }while(0)

#endif /* MACROS_H__ */
