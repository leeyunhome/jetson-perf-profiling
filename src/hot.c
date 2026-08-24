// CPU-bound workload: naive recursive Fibonacci.
//
// Build with -O0 on purpose (see Makefile). fib() must NOT be inlined --
// the deep, self-similar call stack is exactly what makes the flame graph
// interesting, and -O2 optimizes it away.
//
// FIB_N = 42 gives ~2.5s of runtime on a Jetson Orin core, which is long
// enough for perf record to collect ~10k samples. fib(35) finished in
// 0.099s and produced too few samples to be useful.

#include <stdio.h>

#define FIB_N 42

long fib(int n) {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main(void) {
  printf("%ld\n", fib(FIB_N));
  return 0;
}
