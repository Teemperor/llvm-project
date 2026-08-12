#include "plugin.h"

// Same primary template declaration as in main.cpp (deliberately not
// shared via a common header - each module builds its own, independent
// notion of what 'Container' looks like). Crucially, unlike main.cpp,
// this file does *not* define a partial specialization for pointer
// types: 'Container<int *>' below is therefore an ordinary implicit
// instantiation of this primary template, with just two fields ('val',
// 'meta').
template <typename T> struct Container {
  T val;
  int meta;
};

// Instantiates 'Container<int *>' from the *primary* template above
// (there is no partial specialization for pointer types in this
// translation unit). This produces a ClassTemplateSpecializationDecl
// whose "instantiated from" pattern is the primary template, with a
// completely different body/size than main.cpp's 'Container<int *>'
// (which is instantiated from a pointer partial specialization and has
// an extra 'extra' member).
int plugin_int = 444;
Container<int *> plugin_container = {&plugin_int, 555};

extern "C" {
void plugin_init(void) {}

void plugin_entry() {}
}
