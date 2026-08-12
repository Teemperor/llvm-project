#include "plugin.h"

// DylibTwo's 'Config' is spelled identically to DylibOne's, but has two
// 'int' fields instead of one.
struct Config {
  int a;
  int b;
};

Config gConfig2 = {1, 2};

extern "C" void *dylib2_init() { return &gConfig2; }
