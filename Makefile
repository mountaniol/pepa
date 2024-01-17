#GCC=gcc
#CFLAGS=-Wall -Wextra -rdynamic -O2

#GCC=clang-10
#GCC=gcc
GCC=gcc-10
#CFLAGS=-Wall -Wextra -O2 -Wswitch-enum -Wimplicit-fallthrough -Wno-error=unused-but-set-variable \
			-Wswitch -Wreturn-type -Wpedantic -Wformat-overflow=2 -Wformat-nonliteral \
			-Wformat-security -Wformat-signedness -Wnonnull -Wformat-truncation=2 -Wnonnull-compare \
			-Wnull-dereference -Winit-self -pedantic-errors -Wignored-qualifiers -Wno-ignored-attributes \
			-Wmissing-attributes -Wmissing-braces -Wmultistatement-macros -Wparentheses -Wreturn-type \
			-Wswitch-default -Wswitch-enum -Wno-switch-bool -Wno-switch-outside-range -Wno-switch-unreachable \
			-Wunused -Wstrict-overflow=5

#CFLAGS=-Wabi -Wabi-tag -Wabsolute-value \
		-Waddress -Waddress-of-packed-member -Waggregate-return \
		-Waggressive-loop-optimizations -Waliasing -Walign-commons \
		-Walloc-zero -Walloca -Wampersand -Wargument-mismatch \
		-Warray-bounds -Warray-temporaries -Wassign-intercept \
		-Wattribute-warning -Wattributes -Wbad-function-cast -Wbool-compare \
		-Wbool-operation -Wbuiltin-declaration-mismatch -Wbuiltin-macro-redefined \
		-Wc++-compat -Wc++11-compat -Wc++14-compat -Wc++17-compat -Wc-binding-type \
		-Wc11-c2x-compat -Wcannot-profile -Wcast-align \
		-Wcast-align=strict -Wcast-function-type -Wcast-qual -Wcast-result \
		-Wchar-subscripts -Wcharacter-truncation -Wclass-conversion -Wclass-memaccess \
		-Wclobbered -Wcomment -Wcompare-reals -Wconditionally-supported -Wconversion \
		-Wconversion-extra -Wconversion-null -Wcoverage-mismatch -Wcpp -Wctor-dtor-privacy \
		-Wdangling-else -Wdate-time -Wdelete-incomplete \
		-Wdelete-non-virtual-dtor -Wdeprecated -Wdeprecated-copy -Wdeprecated-copy-dtor \
		-Wdeprecated-declarations -Wdesignated-init -Wdisabled-optimization \
		-Wdiscarded-array-qualifiers -Wdiscarded-qualifiers -Wdiv-by-zero -Wdo-subscript \
		-Wdouble-promotion -Wduplicate-decl-specifier -Wduplicated-branches \
		-Wduplicated-cond -Weffc++ -Wempty-body -Wendif-labels -Wenum-compare \
		-Wexpansion-to-defined -Wextra -Wextra-semi -Wfloat-conversion -Wfloat-equal \
		-Wformat-contains-nul -Wformat-extra-args -Wformat-nonliteral -Wformat-security \
		-Wformat-signedness -Wformat-y2k -Wformat-zero-length -Wframe-address \
		-Wfree-nonheap-object -Wfunction-elimination -Whsa -Wif-not-aligned -Wignored-attributes \
		-Wignored-qualifiers -Wimplicit -Wimplicit-function-declaration -Wimplicit-int \
		-Wimplicit-interface -Wimplicit-procedure -Wincompatible-pointer-types \
		-Winherited-variadic-ctor -Winit-list-lifetime -Winit-self -Winline \
		-Wint-conversion -Wint-in-bool-context -Wint-to-pointer-cast -Winteger-division \
		-Wintrinsic-shadow -Wintrinsics-std -Winvalid-memory-model -Winvalid-offsetof \
		-Winvalid-pch -Wjump-misses-init -Wline-truncation -Wliteral-suffix \
		-Wlogical-not-parentheses -Wlogical-op -Wlto-type-mismatch \
		-Wmain -Wmaybe-uninitialized -Wmemset-elt-size -Wmemset-transposed-args \
		-Wmisleading-indentation -Wmissing-attributes -Wmissing-braces -Wmissing-declarations \
		-Wmissing-field-initializers -Wmissing-include-dirs -Wmissing-parameter-type \
		-Wmissing-profile -Wmissing-prototypes -Wmultichar -Wmultiple-inheritance \
		-Wmultistatement-macros -Wnamespaces -Wnarrowing -Wnested-externs -Wnoexcept \
		-Wnoexcept-type -Wnon-template-friend -Wnon-virtual-dtor -Wnonnull \
		-Wnonnull-compare -Wnull-dereference -Wodr -Wold-style-cast -Wold-style-declaration \
		-Wold-style-definition -Wopenmp-simd -Woverflow -Woverlength-strings -Woverloaded-virtual \
		-Woverride-init -Woverride-init-side-effects -Wpacked -Wpacked-bitfield-compat \
		-Wpacked-not-aligned -Wparentheses -Wpedantic -Wpessimizing-move \
		-Wpmf-conversions -Wpointer-arith -Wpointer-compare -Wpointer-sign -Wpointer-to-int-cast \
		-Wpragmas -Wprio-ctor-dtor -Wproperty-assign-default -Wprotocol -Wpsabi -Wreal-q-constant \
		-Wrealloc-lhs -Wrealloc-lhs-all -Wredundant-decls -Wredundant-move -Wregister -Wreorder \
		-Wrestrict -Wreturn-local-addr -Wreturn-type -Wselector -Wsequence-point -Wshadow \
		-Wshadow-ivar -Wshadow=compatible-local -Wshadow=local -Wshift-count-negative \
		-Wshift-count-overflow -Wshift-negative-value -Wsign-compare -Wsign-conversion \
		-Wsign-promo -Wsized-deallocation -Wsizeof-array-argument -Wsizeof-pointer-div \
		-Wsizeof-pointer-memaccess -Wstack-protector -Wstrict-null-sentinel -Wstrict-prototypes \
		-Wstrict-selector-match -Wstringop-truncation -Wsubobject-linkage -Wsuggest-attribute=cold \
		-Wsuggest-attribute=const -Wsuggest-attribute=format -Wsuggest-attribute=malloc \
		-Wsuggest-attribute=noreturn -Wsuggest-attribute=pure -Wsuggest-final-methods \
		-Wsuggest-final-types -Wsuggest-override -Wsurprising -Wswitch -Wswitch-bool \
		-Wswitch-default -Wswitch-enum -Wswitch-unreachable -Wsync-nand -Wsynth \
		-Wsystem-headers -Wtabs -Wtarget-lifetime -Wtautological-compare -Wtemplates \
		-Wterminate -Wtrampolines -Wtrigraphs -Wtype-limits -Wundef \
		-Wuninitialized -Wunknown-pragmas \
		-Wunsuffixed-float-constants -Wunused -Wunused-but-set-parameter \
		-Wunused-but-set-variable -Wunused-function \
		-Wunused-label -Wunused-local-typedefs -Wunused-macros -Wunused-parameter \
		-Wunused-result -Wunused-value -Wunused-variable \
		-Wvarargs -Wvariadic-macros -Wvector-operation-performance \
		-Wvla -Wvolatile-register-var \
		-Wwrite-strings

CFLAGS=-Wabi -Wabsolute-value -Waddress -Waddress-of-packed-member \
		-Walloc-zero -Walloca -Wbool-compare \
		-Wbool-operation -Wbuiltin-declaration-mismatch \
		-Wbuiltin-macro-redefined -Wc11-c2x-compat \
		-Wcast-function-type -Wcast-qual \
		-Wchar-subscripts -Wclobbered -Wcomment -Wconversion -Wcpp \
		-Wdangling-else -Wdate-time \
		-Wdeprecated -Wdesignated-init -Wdiscarded-array-qualifiers \
		-Wdiscarded-qualifiers -Wdiv-by-zero -Wdouble-promotion \
		-Wduplicate-decl-specifier -Wduplicated-branches -Wduplicated-cond \
		-Wempty-body -Wendif-labels -Wenum-compare -Wexpansion-to-defined \
		-Wextra -Wfloat-conversion -Wfloat-equal -Wformat-contains-nul \
		-Wformat-extra-args -Wformat-nonliteral -Wformat-security \
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
		-Wshift-negative-value -Wsign-compare -Wsign-conversion \
		-Wsizeof-array-argument -Wsizeof-pointer-div \
		-Wsizeof-pointer-memaccess -Wstrict-prototypes -Wstringop-truncation \
		-Wsuggest-attribute=format -Wswitch -Wswitch-bool -Wswitch-default \
		-Wswitch-enum -Wsync-nand -Wsystem-headers -Wtautological-compare \
		-Wtrigraphs -Wundef \
		-Wuninitialized -Wunknown-pragmas -Wunsuffixed-float-constants \
		-Wunused -Wunused-local-typedefs -Wunused-macros -Wunused-result \
		-Wunused-variable -Wvarargs -Wvariadic-macros -Wvla # -Wredundant-decls -Wpedantic -Wbad-function-cast

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

%.o:%.c
	@echo "|>" $@...
	@$(GCC) -g $(INCLUDE) $(CFLAGS) $(DEBUG) -c -o $@ $<


