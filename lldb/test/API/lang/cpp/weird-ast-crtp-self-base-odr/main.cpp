#include "plugin.h"

// Classic CRTP (curiously recurring template pattern): 'Base' is templated
// on the derived class itself, so 'Base<Derived>' and 'Derived' are
// mutually dependent in the AST -- 'Derived's base-specifier names a
// specialization of 'Base' whose template argument is 'Derived' itself,
// i.e. the class template specialization is directly cyclic.
//
// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define the exact same CRTP pair ('Base<Derived>'/'Derived'),
// but the dylib's version of 'Base<Derived>' (see plugin.cpp) has the order
// of members swapped and an extra field, giving 'Base<Derived>' two
// genuinely conflicting layouts across the two TUs while 'Derived' in both
// TUs still names 'Base<Derived>' as its base. When an expression pulls in
// both modules' 'Derived'/'Base<Derived>', the ASTImporter has to reconcile
// 'Base<Derived>' as the base-class subobject of a cross-module 'Derived',
// with the added twist that 'Base<Derived>' recursively refers back to
// 'Derived' via its template argument.
template <typename D> struct Base {
  D *self() { return static_cast<D *>(this); }
  int common;
};

struct Derived : Base<Derived> {
  int extra;
};

Derived derived_from_main;

int main() {
  derived_from_main.common = 1;
  derived_from_main.extra = 2;
  plugin_init(&derived_from_main);
  plugin_entry();
  return 0;
}
