#include "DylibB.h"

static Alias gB = {2.0, 3};

extern "C" {
Alias *getB(void) { return &gB; }

// See the comment on getAliasBFromA() in DylibA.cpp: this exists purely
// so that dylib B's DWARF actually emits the 'AliasA' typedef (and the
// incomplete forward-declared 'RealA' it points at).
AliasA *getAliasAFromB(void) { return nullptr; }

void b_init(void) {}
}
