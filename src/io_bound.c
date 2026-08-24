// I/O-bound counterpart to hot.c: almost no arithmetic, but 2M write()
// syscalls, so nearly all of the wall time is spent in kernel mode.
//
// This is the workload that exposed two things (see docs/log.md):
//   - perf stat's ":u" scope hides most of the work here, because the work
//     happens in the kernel. It needs sudo to be measured at all.
//   - ftrace's default ring buffer overflows on this many events, leaving
//     only the last ~22ms of the run.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define ITERATIONS 2000000
#define OUT_PATH "/tmp/io_bound_test.bin"

int main(void) {
  int fd = open(OUT_PATH, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    perror("open " OUT_PATH);
    return 1;
  }

  char buf[64] = {0};
  for (int i = 0; i < ITERATIONS; i++) {
    ssize_t written = write(fd, buf, sizeof(buf));
    if (written != (ssize_t)sizeof(buf)) {
      // A short write is not retried here -- for a benchmark, a partial
      // write means the measurement is invalid, so bail out loudly.
      perror("write");
      close(fd);
      return 1;
    }
  }

  if (close(fd) < 0) {
    perror("close");
    return 1;
  }
  return 0;
}
