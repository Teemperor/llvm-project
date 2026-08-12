// M1 defines this test's shared 'struct Common' with a 'fields' array of
// exactly 1 element(s) -- see common.h.in / the Makefile's per-module sed
// generation of common1.h for how each of the six dylibs gets its own
// distinct array bound while still naming the struct plain 'Common'.
#include "common1.h"

Common gCommon1 = {};

extern "C" Common *MakeCommon1() {
  for (int j = 0; j < 1; ++j)
    gCommon1.fields[j] = 1 * 100 + j;
  return &gCommon1;
}
