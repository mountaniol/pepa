#GCC=gcc
#CFLAGS=-Wall -Wextra -rdynamic -O2

#GCC=clang-10
GCC=gcc

ifneq ($(compiler),)
	GCC=$(compiler)
endif

ifneq ($(linker),)
	LD=$(linker)
endif

#GCC=gcc-10
#CFLAGS=-Wall -Wextra -O2 -Wswitch-enum -Wimplicit-fallthrough -Wno-error=unused-but-set-variable \
			-Wswitch -Wreturn-type -Wpedantic -Wformat-overflow=2 -Wformat-nonliteral \
			-Wformat-security -Wformat-signedness -Wnonnull -Wformat-truncation=2 -Wnonnull-compare \
			-Wnull-dereference -Winit-self -pedantic-errors -Wignored-qualifiers -Wno-ignored-attributes \
			-Wmissing-attributes -Wmissing-braces -Wmultistatement-macros -Wparentheses -Wreturn-type \
			-Wswitch-default -Wswitch-enum -Wno-switch-bool -Wno-switch-outside-range -Wno-switch-unreachable \
			-Wunused -Wstrict-overflow=5

CFLAGS=-Wabsolute-value -Waddress -Waddress-of-packed-member \
		-Walloc-zero -Walloca -Wbool-compare \
		-Wbool-operation -Wbuiltin-declaration-mismatch \
		-Wbuiltin-macro-redefined -Wc11-c2x-compat \
		-Wcast-function-type  \
		-Wchar-subscripts -Wclobbered -Wcomment  -Wcpp \
		-Wdangling-else -Wdate-time \
		-Wdeprecated -Wdesignated-init -Wdiscarded-array-qualifiers \
		-Wdiscarded-qualifiers -Wdiv-by-zero -Wdouble-promotion \
		-Wduplicate-decl-specifier -Wduplicated-branches -Wduplicated-cond \
		-Wempty-body -Wendif-labels -Wenum-compare -Wexpansion-to-defined \
		-Wextra -Wfloat-conversion -Wfloat-equal -Wformat-contains-nul \
		-Wformat-extra-args -Wformat-security \
		-Wformat-signedness -Wformat-y2k -Wformat-zero-length -Wframe-address \
		-Wif-not-aligned -Wignored-attributes -Wignored-qualifiers -Wimplicit \
		-Wimplicit-function-declaration -Wimplicit-int \
		-Wincompatible-pointer-types -Winit-self -Wint-conversion \
		-Wint-in-bool-context -Wint-to-pointer-cast -Winvalid-pch \
		-Wjump-misses-init -Wlogical-not-parentheses -Wlogical-op \
		-Wmain -Wmaybe-uninitialized -Wmemset-elt-size \
		-Wmemset-transposed-args -Wmisleading-indentation \
		-Wmissing-attributes -Wmissing-braces -Wmissing-declarations \
		-Wmissing-field-initializers -Wmissing-include-dirs \
		-Wmissing-parameter-type -Wmissing-prototypes -Wmultichar \
		-Wmultistatement-macros -Wnarrowing -Wnested-externs -Wnonnull \
		-Wnonnull-compare -Wold-style-declaration -Wold-style-definition \
		-Wopenmp-simd -Woverlength-strings -Woverride-init \
		-Woverride-init-side-effects -Wpacked-bitfield-compat \
		-Wpacked-not-aligned -Wparentheses -Wpointer-arith \
		-Wpointer-compare -Wpointer-sign -Wpointer-to-int-cast -Wpragmas \
		-Wprio-ctor-dtor -Wpsabi -Wrestrict -Wreturn-type \
		-Wsequence-point -Wshift-count-negative -Wshift-count-overflow \
		-Wshift-negative-value -Wsign-compare \
		-Wsizeof-array-argument -Wsizeof-pointer-div \
		-Wsizeof-pointer-memaccess -Wstrict-prototypes -Wstringop-truncation \
		-Wsuggest-attribute=format -Wswitch -Wswitch-bool -Wswitch-default \
		-Wswitch-enum -Wsync-nand -Wsystem-headers -Wtautological-compare \
		-Wtrigraphs -Wundef \
		-Wuninitialized -Wunknown-pragmas -Wunsuffixed-float-constants \
		-Wunused -Wunused-local-typedefs -Wunused-macros -Wunused-result \
		-Wunused-variable -Wvarargs -Wvariadic-macros \
		-Wvla 

		# -Wredundant-decls -Wpedantic -Wbad-function-cast -Wformat-nonliteral \
		# -Wsign-conversion -Wconversion -Wcast-qual

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
CFLAGS+=-O2

INC=-I./libconfuse/src/

LIBCONFUSE_A=./libconfuse/src/.libs/libconfuse.a

#PEPA_O= pepa3.o pepa_state_machine.o pepa_parser.o main.o pepa_core.o \
		pepa_server.o pepa_errors.o \
		pepa_socket_common.o pepa_socket_in.o \
		pepa_socket_out.o pepa_socket_shva.o

PEPA_O= pepa_config.o pepa3.o pepa_state_machine.o pepa_parser.o main.o pepa_core.o \
		pepa_server.o pepa_errors.o \
		pepa_socket_common.o pepa_in_reading_sockets.o
PEPA_T=pepa-ng


LIBS=-lpthread $(LIBCONFUSE_A)

AFL_O=pepa_afl.o pepa3.o pepa_state_machine.o pepa_parser.o pepa_core.o \
		pepa_server.o pepa_errors.o pepa_socket_common.o pepa_in_reading_sockets.o

AFL_T=pepa_afl.out

BUFT_AR=buf_t/buf_t.a
SLOG_AR=slog/libslog.a
ARS=$(BUFT_AR) $(SLOG_AR)

#EMU_O=pepa_emulator.o pepa_state_machine.o pepa_parser.o \
	pepa_core.o pepa_server.o pepa_errors.o \
	pepa_socket_common.o pepa_socket_in.o \
	pepa_socket_out.o pepa_socket_shva.o 
EMU_O=pepa_emulator.o pepa_state_machine.o pepa_parser.o \
	pepa_core.o pepa_server.o pepa_errors.o \
	pepa_socket_common.o pepa_in_reading_sockets.o pepa_config.o

EMU_T=emu

#all: pepa emu
all: clean static
ca: clean pepa emu

pepa: slog buf_t $(PEPA_O)
	$(GCC) $(CFLAGS) $(INC) $(DEBUG) $(PEPA_O) $(ARS) -o $(PEPA_T) $(LIBS)

static: slog buf_t $(PEPA_O)
	$(GCC) $(CFLAGS) $(INC) -static $(DEBUG) $(PEPA_O) $(ARS) -o $(PEPA_T) $(LIBS)
	

.PHONY:buf_t
buf_t:
	make -C buf_t

.PHONY:emu
emu: buf_t slog $(EMU_O)
	$(GCC) $(CFLAGS) $(DEBUG) $(EMU_O) $(ARS) -o $(EMU_T) $(LIBS)

#.PHONY:emu
.SECONDARY:$(LIBCONFUSE_A)
$(LIBCONFUSE_A):
	cd libconfuse ; ./autogen.sh ; ./configure ; make clean all ; cd -

.PHONY:slog
slog:
	make -C slog

clean:
	rm -f $(PEPA_T) $(PEPA_O) $(EMU_T) $(EMU_O) $(AFL_T)
	make -C buf_t clean
	make -C slog clean

.PHONY:check
check:
	#@echo "+++ $@: USER=$(USER), UID=$(UID), GID=$(GID): $(CURDIR)"
	@echo ============= 32 bit check =============
	$(ECH)cppcheck -j2 -q --force  --enable=all --platform=unix32 -I/usr/include/openssl ./*.[ch]
	#echo ============= 64 bit check =============
	#$(ECH)cppcheck -q --force  --enable=all --platform=unix64 -I/usr/include/openssl ./*.[ch]

.PHONY:splint
splint:
	@echo "+++ $@: USER=$(USER), UID=$(UID), GID=$(GID): $(CURDIR)"
	#splint -standard -export-local -pred-bool-others -noeffect +matchanyintegral +unixlib -I/usr/include/openssl -D__gnuc_va_list=va_list  ./*.[ch]
	#splint -standard -export-local -pred-bool-others -noeffect +matchanyintegral +unixlib  ./*.[ch]
	splint -weak -pred-bool-others +matchanyintegral +unixlib ./*.[ch]

flaw:
	flawfinder ./*.[ch] 

### AFL fuzzier ###

AFL_PATH=afl_dir

### afl_install: ........ Download the AFL+ fuzzier into the 'afl' directory and compile it; you typically don't need it; just run 'make afl'
afl_install:
ifeq (,$(wildcard $(AFL_PATH)/afl-gcc))
	git clone https://github.com/AFLplusplus/AFLplusplus.git $(AFL_PATH)
	make -C $(AFL_PATH)
endif

### afl_clean: .......... Remove the 'afl' directory
afl_clean:
	rm $(AFL_PATH) -fr


#$(AFL_PATH):
#	make afl_install

### afl: ................ Download and build the AFL+ fuzzier, then build the AFL+ fuzzier-related code and run the fuzzier
#.PHONY:afl
#afl: $(AFL_PATH)/afl-gcc
$(AFL_PATH)/afl-gcc: #$(AFL_PATH)
	@echo Executing target $@
	make afl_install
	@echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	#compiler=afl-clang-fast linker=afl-clang-fast AFL_HARDEN=1 make _fuzzer
	#compiler=$(AFL_PATH)/afl-gcc linker=$(AFL_PATH)/afl-clang-fast AFL_HARDEN=1 make _fuzzer
	#rm -fr ./fuzzer_output
	#$(AFL_PATH)/afl-fuzz -t 10000 -i ./fuzzer_input -o ./fuzzer_output ./$(AFL_T)

$(AFL_PATH)/afl-fuzz:
	make afl_install

_fuzzer: slog buf_t $(AFL_O) #$(AFL_PATH)/afl-gcc
	@echo Compiling target $@
	@echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	$(AFL_PATH)/afl-gcc $(CFLAGS) $(FUZZER_DEBUG) $(AFL_O) $(BUFT_AR) $(SLOG_AR) -lpthread -o $(AFL_T)
	#$(AFL_PATH)/afl-clang-fast $(CFLAGS) $(FUZZER_DEBUG) $(AFL_O) $(BUFT_AR) $(SLOG_AR) -lpthread -o $(AFL_T)


#.PHONY:$(AFL_PATH)/afl-gcc
fuzzer: $(AFL_PATH)/afl-gcc
	@echo Executing target $@
	@echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	#compiler=afl-clang-fast linker=afl-clang-fast AFL_HARDEN=1 make _fuzzer
	compiler=$(AFL_PATH)/afl-gcc linker=$(AFL_PATH)/afl-gcc AFL_HARDEN=1 make _fuzzer
	#compiler=$(AFL_PATH)/afl-clang-fast linker=$(AFL_PATH)/afl-clang-fast AFL_HARDEN=1 make _fuzzer
	rm -fr ./fuzzer_output
	AFL_MAP_SIZE=10000000 $(AFL_PATH)/afl-fuzz -t 10000 -i fuzzer_input -o ./fuzzer_output ./$(AFL_T)

%.o:%.c
	@echo "|>" $@...
	@$(GCC) -g $(INC) $(CFLAGS) $(DEBUG) -c -o $@ $<


