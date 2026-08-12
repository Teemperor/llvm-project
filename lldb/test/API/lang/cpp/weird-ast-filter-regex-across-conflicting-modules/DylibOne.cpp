#include "plugin.h"

// DylibOne's version of 'Config' has a single 'int' member. Three other
// dylibs loaded into the very same process (DylibTwo/DylibThree/DylibFour)
// each define an identically-named 'struct Config' with a different
// number of 'int' fields (2, 3, and 4 respectively) -- a deliberate ODR
// violation spread across four separate DWARFASTParserClang instances,
// one per dylib.
struct Config {
  int a;
};

Config gConfig1 = {1};

extern "C" void *dylib1_init() { return &gConfig1; }
