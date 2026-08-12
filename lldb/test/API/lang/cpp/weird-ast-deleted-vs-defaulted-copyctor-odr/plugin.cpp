#include "plugin.h"

// This is a DIFFERENT (ODR-violating) definition of 'Widget' compared to
// the one in main.cpp: same name and same data member, but here the copy
// constructor and copy assignment operator are explicitly defaulted (and
// therefore NOT deleted), whereas the exe's 'Widget' explicitly deletes
// both. Both definitions have identical layout (a single 'int x' member),
// so this is a pure "deleted-ness"/special-member-flags ODR conflict, not
// a layout or vtable-shape conflict.
struct Widget {
  int x;
  Widget() : x(10) {}
  Widget(const Widget &) = default;
  Widget &operator=(const Widget &) = default;
};

// Global so debug-info for this (conflicting) definition of 'Widget' is
// also emitted with a full definition, in the dylib's compile unit.
Widget w2;

extern "C" {
void plugin_init() { w2.x = 20; }

void plugin_entry() {}
}
