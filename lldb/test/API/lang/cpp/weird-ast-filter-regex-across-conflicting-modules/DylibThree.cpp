#include "plugin.h"

// DylibThree's 'Config' is spelled identically to the others, but has
// three 'int' fields.
struct Config {
  int a;
  int b;
  int c;
};

Config gConfig3 = {1, 2, 3};

extern "C" void *dylib3_init() { return &gConfig3; }
