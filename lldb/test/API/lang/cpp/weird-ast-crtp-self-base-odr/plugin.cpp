#include "plugin.h"

// See main.cpp: this is the same CRTP pair ('Base<Derived>'/'Derived') as
// in main.cpp, but 'Base<Derived>' here has the order of 'common' and an
// extra field ('extra_in_base') swapped in, giving 'Base<Derived>' a
// different layout than the main executable's 'Base<Derived>', while
// 'Derived' still names 'Base<Derived>' as its base in both TUs.
template <typename D> struct Base {
  D *self() { return static_cast<D *>(this); }
  int extra_in_base;
  int common;
};

struct Derived : Base<Derived> {
  int extra;
};

Derived *derived_from_plugin;

extern "C" {
void plugin_init(void *derived_from_main_ptr) {
  derived_from_plugin = new Derived();
  derived_from_plugin->common = 3;
  derived_from_plugin->extra_in_base = 4;
  derived_from_plugin->extra = 5;
  // Unused; passed in solely so main.cpp's 'Derived' (and its 'common'
  // member) is reachable from an expression evaluated inside the dylib.
  (void)derived_from_main_ptr;
}

void plugin_entry() {}
}
