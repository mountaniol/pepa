#GCC=gcc
#CFLAGS=-Wall -Wextra -rdynamic -O2

#GCC=clang-10
GCC=gcc
CFLAGS=-Wall -Wextra -O2
#CFLAGS=-Wall -Wextra -O2
DEBUG=-DDEBUG3
#CFLAGS += -fanalyzer

#GCCVERSION=$(shell gcc -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$/&00/')

#ifeq "$(GCCVERSION)" "10"
#	CFLAGS += --fanalyzer
#endif

# client daemon

PEPA_O=main.o
PEPA_T=pepa

all: pepa

pepa: $(PEPA_O)
	$(GCC) $(CFLAGS) -ggdb $(DEBUG) $(PEPA_O) -o $(PEPA_T) -lpthread

clean:
	rm -f $(PEPA_T) $(PEPA_O)

%.o:%.c
	@echo "|>" $@...
	@$(GCC) -g $(INCLUDE) $(CFLAGS) $(DEBUG) -c -o $@ $<


