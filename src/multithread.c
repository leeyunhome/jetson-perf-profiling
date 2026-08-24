// Parallel counterpart to hot.c: NTHREADS threads each run the same
// recursive fib(). This is the workload that finally produces non-zero
// context-switches and cpu-migrations in perf stat -- the three
// single-threaded workloads all reported 0.
//
// FIB_N is 38 rather than 42 so total runtime stays in the same ballpark
// as the single-threaded run instead of taking minutes.

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NTHREADS 4
#define FIB_N 38

long fib(int n) {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

void *worker(void *arg) {
  (void)arg; // unused: every thread runs the identical workload
  printf("thread result: %ld\n", fib(FIB_N));
  return NULL;
}

int main(void) {
  pthread_t threads[NTHREADS];
  int created = 0;

  for (int i = 0; i < NTHREADS; i++) {
    // pthread_create returns the error number directly and does not set
    // errno, so strerror(rc) rather than perror().
    int rc = pthread_create(&threads[i], NULL, worker, NULL);
    if (rc != 0) {
      fprintf(stderr, "pthread_create(%d): %s\n", i, strerror(rc));
      break;
    }
    created++;
  }

  // Only join the threads that were actually created, so a partial
  // failure above does not turn into a join on an uninitialized handle.
  for (int i = 0; i < created; i++) {
    int rc = pthread_join(threads[i], NULL);
    if (rc != 0) {
      fprintf(stderr, "pthread_join(%d): %s\n", i, strerror(rc));
    }
  }

  if (created != NTHREADS) {
    fprintf(stderr, "only %d/%d threads ran -- measurement is invalid\n",
            created, NTHREADS);
    return 1;
  }
  return 0;
}
