// M2 defines this test's shared 'struct Common' with a 'fields' array of
// exactly 2 element(s) -- see common.h.in / the Makefile's per-module sed
// generation of common2.h for how each of the six dylibs gets its own
// distinct array bound while still naming the struct plain 'Common'.
#include "common2.h"

Common gCommon2 = {};

extern "C" Common *MakeCommon2() {
  for (int j = 0; j < 2; ++j)
    gCommon2.fields[j] = 2 * 100 + j;
  return &gCommon2;
}
