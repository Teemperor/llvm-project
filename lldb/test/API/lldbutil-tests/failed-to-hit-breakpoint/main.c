#include <stdio.h>

int main(int argc, char **argv) {
  // Prin the string that the test looks for to make sure stderr got recorded.
  fprintf(stderr, "stderr_needle\n");
  // This is unreachable during normal test execution as we don't pass any
  // (or +100) arguments. This still needs to be theoretically reachable code
  // so that the compiler will generate code for this (that we can set a
  // breakpoint on).
  if (argc > 100)
    return 1; // break here
  return 0;
}
