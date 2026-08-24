#include <pthread.h>
#include <stdio.h>

long fib(int n) {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

void *worker(void *arg) {
  long r = fib(38);
  printf("thread result: %ld\n", r);
  return NULL;
}

int main() {
  int nthreads = 4;
  pthread_t threads[nthreads];
  for (int i = 0; i < nthreads; i++) {
    pthread_create(&threads[i], NULL, worker, NULL);
  }
  for (int i = 0; i < nthreads; i++) {
    pthread_join(threads[i], NULL);
  }
  return 0;
}
