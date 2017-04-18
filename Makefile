LIBS =
SRCS = rb.c
MAIN = librb

STD = -std=c89
WARN = -Wall -Wextra -pedantic
CFLAGS = $(STD) $(WARN) -fPIC
LDFLAGS = -shared

VERSION = `grep "define VERSION" version.h | cut -d \" -f2`
VERSION_MAJOR = `grep "define VERSION" version.h | cut -d \" -f2 | cut -d. -f1`
DESTDIR ?= `cat .destdir`
CC ?=
INC ?=
LINC ?=
THREADS ?=

ifdef THREADS
	LIBS += -lpthread
	CFLAGS += -DLIBRB_PTHREAD
endif


OBJS = $(SRCS:.c=.o)

.PHONY: depend clean debug release

all: release

release: CFLAGS += -O2
release: $(MAIN)

debug: CFLAGS += -O0 -ggdb -g3
debug: $(MAIN)

test: LDFLAGS=
test: $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(LINC) $(LDFLAGS) -o librb-test $(OBJS) test.c
	@./librb-test

install:
	install -m 0644 -D -t $(DESTDIR)/include rb.h
	install -m 0755 -D -t $(DESTDIR)/lib     $(MAIN).so.$(VERSION)
	ln -sf $(MAIN).so.$(VERSION) $(DESTDIR)/lib/$(MAIN).so.$(VERSION_MAJOR)
	ln -sf $(MAIN).so.$(VERSION) $(DESTDIR)/lib/$(MAIN).so
	echo $(DESTDIR) > .destdir

uninstall:
	$(RM) $(DESTDIR)/include/rb.h
	$(RM) $(DESTDIR)/lib/$(MAIN).so.$(VERSION)
	@if [ ! -f $(DESTDIR)/lib/$(MAIN).so.$(VERSION_MAJOR) ]; then \
	    $(RM) $(DESTDIR)/lib/$(MAIN).so.$(VERSION_MAJOR); \
	fi
	@if [ ! -f $(DESTDIR)/lib/$(MAIN).so ]; then \
	    $(RM) $(DESTDIR)/lib/$(MAIN).so; \
	fi

clean:
	$(RM) *.o *~ $(MAIN).so.$(VERSION) librb-test

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(LINC) $(LDFLAGS) -o $(MAIN).so.$(VERSION) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) $(INC) -c $< -o $@
