CFLAGS += -I$(top_srcdir)
EXTRA_PROGRAMS = event_loop \
	posix_network_parser \
	prod_cons_threads \
	simple_read_write

common_src = rb.c common.c
event_loop_SOURCES = $(common_src) event-loop.c
posix_network_parser_SOURCES = $(common_src) posix-network-parser.c
prod_cons_threads_SOURCES = $(common_src) prod-cons-threads.c
simple_read_write_SOURCES = $(common_src) simple-read-write.c

examples: $(EXTRA_PROGRAMS)

clean-local:
	rm -f $(EXTRA_PROGRAMS)
