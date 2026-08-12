#include "plugin.h"

// Small version of Variant's anonymous union as seen by the main
// executable. Note that this definition is intentionally different from
// the one used inside the dylib (plugin.cpp): the anonymous union here only
// has two members, while the dylib's anonymous union has four (and is much
// larger). Since the union is anonymous, Clang injects IndirectFieldDecls
// for `i` and `f` directly into Variant's scope. Merging the two
// differently-shaped RecordDecls for Variant via the ASTImporter has to
// reconcile those IndirectFieldDecl chains, which is the mechanism this
// test wants to exercise.
struct Variant {
  int tag;
  union {
    int i;
    float f;
  };
};

Variant main_variant = {0, {42}};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
