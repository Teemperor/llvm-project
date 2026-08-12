// M3 defines this test's shared 'struct Common' with a 'fields' array of
// exactly 3 element(s) -- see common.h.in / the Makefile's per-module sed
// generation of common3.h for how each of the six dylibs gets its own
// distinct array bound while still naming the struct plain 'Common'.
#include "common3.h"

Common gCommon3 = {};

extern "C" Common *MakeCommon3() {
  for (int j = 0; j < 3; ++j)
    gCommon3.fields[j] = 3 * 100 + j;
  return &gCommon3;
}
