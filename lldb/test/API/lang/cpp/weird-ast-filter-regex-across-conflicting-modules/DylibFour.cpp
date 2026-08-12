#include "plugin.h"

// DylibFour's 'Config' is spelled identically to the others, but has
// four 'int' fields -- the most-diverged of the four ODR-conflicting
// definitions.
struct Config {
  int a;
  int b;
  int c;
  int d;
};

Config gConfig4 = {1, 2, 3, 4};

extern "C" void *dylib4_init() { return &gConfig4; }
