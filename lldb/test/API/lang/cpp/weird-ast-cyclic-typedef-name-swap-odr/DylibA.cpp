#include "DylibA.h"

static Alias gA = {1};

extern "C" {
Alias *getA(void) { return &gA; }

// Never actually called with a real dylib-B object; this function only
// exists so that the *type* 'AliasB' (and therefore the incomplete
// forward-declared 'RealB' it points at) shows up in dylib A's DWARF at
// all. An otherwise-unused typedef/forward declaration gets optimized out
// of the debug info entirely.
AliasB *getAliasBFromA(void) { return nullptr; }

void a_init(void) {}
}
