// M6 defines this test's shared 'struct Common' with a 'fields' array of
// exactly 6 element(s) -- see common.h.in / the Makefile's per-module sed
// generation of common6.h for how each of the six dylibs gets its own
// distinct array bound while still naming the struct plain 'Common'.
#include "common6.h"

Common gCommon6 = {};

extern "C" Common *MakeCommon6() {
  for (int j = 0; j < 6; ++j)
    gCommon6.fields[j] = 6 * 100 + j;
  return &gCommon6;
}
