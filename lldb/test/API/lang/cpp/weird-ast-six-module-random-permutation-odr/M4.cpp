// M4 defines this test's shared 'struct Common' with a 'fields' array of
// exactly 4 element(s) -- see common.h.in / the Makefile's per-module sed
// generation of common4.h for how each of the six dylibs gets its own
// distinct array bound while still naming the struct plain 'Common'.
#include "common4.h"

Common gCommon4 = {};

extern "C" Common *MakeCommon4() {
  for (int j = 0; j < 4; ++j)
    gCommon4.fields[j] = 4 * 100 + j;
  return &gCommon4;
}
