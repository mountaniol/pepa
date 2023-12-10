GCC=gcc
#GCC=gcc-10
AR=ar
#GCC=clang-10
CFLAGS=-Wall -Wextra -Wpedantic -rdynamic -O2
#DEBUG=-DDEBUG2
DEBUG=-DDEBUG3
#DEBUG += -DDERROR
#CFLAGS += -fanalyzer

BUF_O=buf_t_errors.o buf_t_array.o buf_t.o buf_t_string.o buf_t_memory.o buf_t_stats.o
BUF_A=buf_t.a
BUF_TEST=btest.out

all: buf
	@echo "DEBUG = $(DEBUG)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "Compiler = $(GCC)"

buf: $(BUF_O)
	@echo Done
	$(AR) rcs $(BUF_A) $(BUF_O)

btest: $(BUF_O)
	$(GCC) $(CFLAGS) -ggdb -DSTANDALONE $(DEBUG) $(BUF_O) buf_t_test.c  -o $(BUF_TEST)

btest_a: buf
	$(GCC) $(CFLAGS) -ggdb $(DEBUG) buf_t_test.c $(BUF_A) -o $(BUF_TEST)

clean:
	rm -f *.o $(BUF_A) $(BUF_TEST)

%.o:%.c
	@echo "|>" $@...
	@$(GCC) -g $(INCLUDE) $(CFLAGS) $(DEBUG) -c -o $@ $<


