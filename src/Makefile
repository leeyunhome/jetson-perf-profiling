# Build the three profiling workloads.
#
# -O0 is deliberate, not an oversight: at -O2 the compiler inlines the
# recursive fib() and the flame graph loses the stack depth that makes it
# readable (see docs/log.md). Keep it at -O0 to reproduce the published
# numbers.
#
# Toolchain used for the published results: gcc 13 on aarch64 Ubuntu 24.04
# (L4T userspace). Nothing here is Tegra-specific -- only the perf binary
# path in ../scripts/bench.sh is.

CC     ?= gcc
CFLAGS ?= -O0 -g -Wall -Wextra

BINS = hot io_bound multithread

.PHONY: all clean format
all: $(BINS)

hot: hot.c
	$(CC) $(CFLAGS) -o $@ $<

io_bound: io_bound.c
	$(CC) $(CFLAGS) -o $@ $<

# -pthread affects both the compile and the link step, so it goes on the
# single gcc invocation rather than in LDLIBS.
multithread: multithread.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

clean:
	rm -f $(BINS)

format:
	clang-format -i *.c
