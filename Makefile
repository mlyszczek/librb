LIBS = -lpthread
SRCS = rb.c
MAIN = librb

STD = -std=c89
WARN = -Wall -Wextra -pedantic -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wwrite-strings -Winit-self -Wcast-align -Wstrict-aliasing -Wformat=2 -Wmissing-include-dirs -Wno-unused-parameter -Wuninitialized -Wstrict-overflow=5 -pedantic-errors
CFLAGS = $(STD) $(WARN) -fPIC
LDFLAGS = -shared

VERSION = `grep "define VERSION" version.h | cut -d \" -f2`
VERSION_MAJOR = `grep "define VERSION" version.h | cut -d \" -f2 | cut -d. -f1`
DESTDIR ?= `cat .destdir`
CC ?=
INC ?=
LINC ?=

OBJS = $(SRCS:.c=.o)

.PHONY: depend clean debug release

all: release

release: WARN += -Werror
release: CFLAGS += -O2
release: $(MAIN)

debug: CFLAGS += -O0 -ggdb -g3
debug: $(MAIN)

install:
	install -m 0644 rb.h $(DESTDIR)/include
	install -m 0755 $(MAIN).so.$(VERSION) $(DESTDIR)/lib
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
	$(RM) *.o *~ $(MAIN).so.$(VERSION)

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(LINC) $(LDFLAGS) -o $(MAIN).so.$(VERSION) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) $(INC) -c $< -o $@
