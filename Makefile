#GCC=gcc
#CFLAGS=-Wall -Wextra -rdynamic -O2

#GCC=clang-10
#GCC=gcc
GCC=gcc-10
CFLAGS=-Wall -Wextra -O2
#CFLAGS=-Wall -Wextra -O2
DEBUG=-DDEBUG3
# Static GCC-10 analyzer
#CFLAGS += -fanalyzer

# Clang static analyzer
#CFLAGS += -Xfanalyzer

PEPA_O=main.o pepa_core.o pepa_server.o pepa_socket.o pepa_errors.o
PEPA_T=pepa
BUFT_AR=buf_t/buf_t.a

all: pepa

pepa: buf_t $(PEPA_O)
	$(GCC) $(CFLAGS) -ggdb $(DEBUG) $(PEPA_O) $(BUFT_AR) -o $(PEPA_T) -lpthread

.PHONY:buf_t
buf_t:
	make -C buf_t

clean:
	rm -f $(PEPA_T) $(PEPA_O)
	make -C buf_t clean

%.o:%.c
	@echo "|>" $@...
	@$(GCC) -g $(INCLUDE) $(CFLAGS) $(DEBUG) -c -o $@ $<


