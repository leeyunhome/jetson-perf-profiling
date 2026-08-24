// I/O-bound counterpart to hot.c: almost no arithmetic, but 2M write()
// syscalls, so nearly all of the wall time is spent in kernel mode.
//
// This is the workload that exposed two things (see docs/log.md):
//   - perf stat's ":u" scope hides most of the work here, because the work
//     happens in the kernel. It needs sudo to be measured at all.
//   - ftrace's default ring buffer overflows on this many events, leaving
//     only the last ~22ms of the run.
//
// Output path: the first version hardcoded /tmp/io_bound_test.bin, which
// broke as soon as the same benchmark ran both as a normal user and under
// sudo. /tmp is world-writable and sticky, and Linux's
// fs.protected_regular refuses an O_CREAT open of an existing file in such
// a directory when the file is owned by neither the caller nor the
// directory owner -- so the root run got EACCES on the file the user run
// had left behind. The path is now per-uid (and overridable), and the file
// is unlinked first so every run starts from a fresh file rather than
// re-truncating whatever the previous run left.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ITERATIONS 2000000
#define OUT_DIR "/tmp"

int main(void) {
  char path[PATH_MAX];
  const char *override = getenv("IO_BOUND_OUT");

  if (override != NULL) {
    snprintf(path, sizeof(path), "%s", override);
  } else {
    snprintf(path, sizeof(path), "%s/io_bound_test.%u.bin", OUT_DIR,
             (unsigned)getuid());
  }

  // Start from a fresh file. Missing file is the normal case, so only a
  // real failure is worth reporting.
  if (unlink(path) < 0 && errno != ENOENT) {
    perror("unlink");
    return 1;
  }

  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    perror(path);
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

  printf("%s: %d writes of %zu bytes\n", path, ITERATIONS, sizeof(buf));
  return 0;
}
