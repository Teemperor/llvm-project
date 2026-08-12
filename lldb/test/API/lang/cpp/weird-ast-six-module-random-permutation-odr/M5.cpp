// M5 defines this test's shared 'struct Common' with a 'fields' array of
// exactly 5 element(s) -- see common.h.in / the Makefile's per-module sed
// generation of common5.h for how each of the six dylibs gets its own
// distinct array bound while still naming the struct plain 'Common'.
#include "common5.h"

Common gCommon5 = {};

extern "C" Common *MakeCommon5() {
  for (int j = 0; j < 5; ++j)
    gCommon5.fields[j] = 5 * 100 + j;
  return &gCommon5;
}
