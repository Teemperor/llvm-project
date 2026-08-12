#include "plugin.h"

// Primary class template.
//
// This file *also* defines a partial specialization for pointer types
// below, so 'Container<int *>' as instantiated in this translation unit
// comes from the partial specialization, not from this primary template
// pattern ('T val; int meta;').
template <typename T> struct Container {
  T val;
  int meta;
};

// Partial specialization for all pointer types. This is a
// ClassTemplatePartialSpecializationDecl: its instantiations are
// ClassTemplateSpecializationDecls whose "instantiated from" pointer
// (getInstantiatedFrom()) refers to this partial specialization, not to
// the primary template above. It also has a different body/size than the
// primary template (an extra 'extra' member).
template <typename T> struct Container<T *> {
  T *val;
  int meta;
  long extra;
};

// Instantiates 'Container<int *>' from the *partial specialization*
// above: this global's type is a ClassTemplateSpecializationDecl with
// three fields ('val', 'meta', 'extra') whose pattern is the partial
// specialization, not the primary template.
//
// See plugin.cpp for the crucial twist: it defines only the primary
// template (no partial specialization for pointer types) and still
// instantiates 'Container<int *>', so its 'Container<int *>' comes from
// the *primary* template pattern instead and only has two fields ('val',
// 'meta'). Both modules therefore produce a type with the exact same
// name, the exact same mangled name and the exact same template argument
// list ('int *'), but with structurally unrelated defining templates
// (ClassTemplatePartialSpecializationDecl-derived vs. plain primary
// ClassTemplateDecl-derived) and different bodies/sizes. This is a case
// the ASTImporter's ODR-checking/merging logic may not expect: it is
// normally used to reconcile two *implicit instantiations of the same
// template pattern* that merely disagree on field layout, not two
// instantiations whose "this came from a partial specialization" bit
// differs while everything else about the specialization (name, mangling,
// template arguments) looks identical.
int main_int = 111;
Container<int *> main_container = {&main_int, 222, 333};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
