#include "plugin.h"

// Same primary template declaration as in main.cpp (deliberately not
// shared via a common header - each module builds its own, independent
// notion of what 'Holder<int>' looks like).
template <typename T> struct Holder {
  T v;
};

// Explicit full specialization of 'Holder<int>', provided and used here
// *before* any implicit instantiation of 'Holder<int>' from the primary
// template happens in this module. Its body is completely different from
// (and larger than) what the primary template would have produced.
//
// Clang represents this very differently from an implicit instantiation:
// this ClassTemplateSpecializationDecl has
// TemplateSpecializationKind::TSK_ExplicitSpecialization and its members
// come straight from this definition, not from instantiating the primary
// template's pattern.
template <> struct Holder<int> {
  int v;
  int extra;
  long tag;
};

// Uses the dylib's explicit specialization of 'Holder<int>'.
Holder<int> plugin_holder = {22, 33, 44};

extern "C" {
void plugin_init(void) {}

void plugin_entry() {}
}
