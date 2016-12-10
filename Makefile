LIBS = -lpthread
SRCS = rb.c
MAIN = librb

STD = -std=c89
WARN = -Wall -Wextra -pedantic -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wwrite-strings -Winit-self -Wcast-align -Wstrict-aliasing -Wformat=2 -Wmissing-include-dirs -Wno-unused-parameter -Wuninitialized -Wstrict-overflow=5 -pedantic-errors
CFLAGS = $(STD) $(WARN) -fPIC
LDFLAGS = -shared

VERSION = `grep "define VERSION" version.h | cut -d \" -f2`
VERSION_MAJOR = `grep "define VERSION" version.h | cut -d \" -f2 | cut -d. -f1`
INSTALL_DIR ?= `cat .install_dir`
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
	mkdir -p $(INSTALL_DIR)/include
	mkdir -p $(INSTALL_DIR)/lib
	cp rb.h $(INSTALL_DIR)/include
	cp $(MAIN).so.$(VERSION) $(INSTALL_DIR)/lib
	ln -sf $(MAIN).so.$(VERSION) $(INSTALL_DIR)/lib/$(MAIN).so.$(VERSION_MAJOR)
	ln -sf $(MAIN).so.$(VERSION) $(INSTALL_DIR)/lib/$(MAIN).so
	echo $(INSTALL_DIR) > .install_dir

uninstall:
	@echo $(INSTALL_DIR)
	rm -f $(INSTALL_DIR)/include/rb.h
	rm -f $(INSTALL_DIR)/lib/$(MAIN).so.$(VERSION)
	@if [ ! -f $(INSTALL_DIR)/lib/$(MAIN).so.$(VERSION_MAJOR) ]; then \
	    rm -f $(INSTALL_DIR)/lib/$(MAIN).so.$(VERSION_MAJOR); \
	fi
	@if [ ! -f $(INSTALL_DIR)/lib/$(MAIN).so ]; then \
	    rm -f $(INSTALL_DIR)/lib/$(MAIN).so; \
	fi

clean:
	$(RM) *.o *~ $(MAIN).so.$(VERSION)

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(LINC) $(LDFLAGS) -o $(MAIN).so.$(VERSION) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) $(INC) -c $< -o $@
