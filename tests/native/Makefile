include ../cases/Makefile.include

OPTFLAGS ?= -O0

CPPFLAGS += -I../../src/include/
CFLAGS += -Wall -Werror -Wpedantic -std=c11
CFLAGS += -g3
CFLAGS += -MD
CFLAGS += ${OPTFLAGS}

CFLAGS += -fsanitize=undefined -fsanitize=address
LDFLAGS += -fsanitize=undefined -fsanitize=address

all: test

vpath %.c ../../src/
vpath %.c ../cases/

include Makefile.mockoap
include Makefile.libcose

BACKEND_OBJS += testwrapper.c

test: ${CASES}
	set -ex; for x in $^; do ./$$x; done

test-all-versions:
	${MAKE} clean
	${MAKE} CC=gcc OPTFLAGS=-O0 test
	${MAKE} clean
	${MAKE} CC=gcc OPTFLAGS=-O3 test
	${MAKE} clean
	${MAKE} CC=clang test
	${MAKE} clean

include $(wildcard *.d)

standalone-demo: standalone-demo.o ${BACKEND_OBJS}

cryptobackend-aead: cryptobackend-aead.o ${BACKEND_OBJS}

unprotect-demo: unprotect-demo.o contextpair.o protection.o oscore_message.o ${BACKEND_OBJS}

unit-contextpair-window: unit-contextpair-window.o contextpair.o ${BACKEND_OBJS}

libs:
	mkdir libs
	# not created as submodules as those are expected to be used in full
	# library fashion on the long run
	cd libs && git clone https://github.com/bergzand/libcose
	cd libs && git clone https://github.com/bergzand/NanoCBOR nanocbor

clean:
	rm -f standalone-demo
	rm -f cryptobackend-aead
	rm -f unprotect-demo
	rm -f *.o
	rm -f *.d

	# Not cleaning libs: there may be user edits in them.

distclean: clean
	rm -rf libs

# recommended GNU Make rule for building dependency files
%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -M $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$


.PHONY: test clean distclean