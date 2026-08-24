#include <stdio.h>
long fib(int n){ return n<2 ? n : fib(n-1)+fib(n-2); }
int main(){ printf("%ld\n", fib(42)); return 0; }
