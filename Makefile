#GCC=gcc
#CFLAGS=-Wall -Wextra -rdynamic -O2

#GCC=clang-10
GCC=gcc
#GCC=gcc-10
CFLAGS=-Wall -Wextra -O2 -Wswitch-enum -Wimplicit-fallthrough -Wno-error=unused-but-set-variable \
			-Wswitch -Wreturn-type -Wpedantic -Wformat-overflow=2 -Wformat-nonliteral \
			-Wformat-security -Wformat-signedness -Wnonnull

#CFLAGS=-Wall -Wextra -O2
#DEBUG=-DDEBUG3
#DEBUG+=-ggdb
# Static GCC-10 analyzer
#CFLAGS+=-fanalyzer
#CFLAGS+=-pg 
# Clang static analyzer
#CFLAGS += -Xfanalyzer

PEPA_VERSION_GIT_VAL=$(shell git log -1 --pretty=format:%h --abbrev=8)
PEPA_BRANCH_GIT_VAL=$(shell git branch --show-current)
PEPA_COMP_DATE_VAL=$(shell date +%F/%H-%M-%S)
#PEPA_COMP_DATE_VAL=$(shell date)
PEPA_USER_VAL=$(shell whoami)
PEPA_HOST_VAL=$(shell hostname)

PEPA_DEFINES=-DPEPA_VERSION_GIT=\"$(PEPA_VERSION_GIT_VAL)\"
PEPA_DEFINES+=-DPEPA_BRANCH_GIT=\"$(PEPA_BRANCH_GIT_VAL)\"
PEPA_DEFINES+=-DPEPA_COMP_DATE=\"$(PEPA_COMP_DATE_VAL)\"
PEPA_DEFINES+=-DPEPA_USER=\"$(PEPA_USER_VAL)\"
PEPA_DEFINES+=-DPEPA_HOST=\"$(PEPA_HOST_VAL)\"

#CFLAGS+= -DPEPA_VERSION_GIT=\"$(PEPA_VERSION_GIT_VAL)\"
CFLAGS+=$(PEPA_DEFINES)

#PEPA_O= pepa3.o pepa_state_machine.o pepa_parser.o main.o pepa_core.o \
		pepa_server.o pepa_errors.o \
		pepa_socket_common.o pepa_socket_in.o \
		pepa_socket_out.o pepa_socket_shva.o

PEPA_O= pepa3.o pepa_state_machine.o pepa_parser.o main.o pepa_core.o \
		pepa_server.o pepa_errors.o \
		pepa_socket_common.o 
		
PEPA_T=pepa-ng
BUFT_AR=buf_t/buf_t.a
SLOG_AR=slog/libslog.a
ARS=$(BUFT_AR) $(SLOG_AR)

#EMU_O=pepa_emulator.o pepa_state_machine.o pepa_parser.o \
	pepa_core.o pepa_server.o pepa_errors.o \
	pepa_socket_common.o pepa_socket_in.o \
	pepa_socket_out.o pepa_socket_shva.o 
EMU_O=pepa_emulator.o pepa_state_machine.o pepa_parser.o \
	pepa_core.o pepa_server.o pepa_errors.o \
	pepa_socket_common.o 

EMU_T=emu

#all: pepa emu
all: clean static
ca: clean pepa emu

pepa: slog buf_t $(PEPA_O)
	$(GCC) $(CFLAGS) $(DEBUG) $(PEPA_O) $(ARS) -o $(PEPA_T) -lpthread

static: slog buf_t $(PEPA_O)
	$(GCC) $(CFLAGS) -static $(DEBUG) $(PEPA_O) $(ARS) -o $(PEPA_T) -lpthread
	

.PHONY:buf_t
buf_t:
	make -C buf_t

.PHONY:emu
emu: buf_t slog $(EMU_O)
	$(GCC) $(CFLAGS) $(DEBUG) $(EMU_O) $(ARS) -o $(EMU_T) -lpthread

.PHONY:slog
slog:
	make -C slog

clean:
	rm -f $(PEPA_T) $(PEPA_O) $(EMU_T) $(EMU_O)
	make -C buf_t clean
	make -C slog clean

%.o:%.c
	@echo "|>" $@...
	@$(GCC) -g $(INCLUDE) $(CFLAGS) $(DEBUG) -c -o $@ $<


