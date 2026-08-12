#include "plugin.h"

// See main.cpp for 'Handle'. Deliberately redefined identically here (not
// shared via a common header) so both modules get their own full
// definition in their own debug info.
struct Handle {
  int *p;
  Handle(int *p) : p(p) {}
  Handle(Handle &&other) : p(other.p) { other.p = nullptr; }
  Handle(const Handle &other) : p(other.p) {}
  ~Handle() {}
};

// This is a DIFFERENT (ODR-violating) definition of 'Resource' compared to
// the one in main.cpp: same name and same data member, but here the copy
// constructor is explicitly defaulted (usable), which suppresses the
// implicitly-declared move constructor -- so THIS 'Resource' has no move
// constructor at all. This is the exact opposite of main.cpp's 'Resource',
// which defaults its move constructor and has no copy constructor.
struct Resource {
  Handle h;
  Resource(int *p) : h(p) {}
  Resource(const Resource &) = default;
};

// Globals so debug-info for this (conflicting) definition of 'Resource' is
// also emitted with a full definition, in the dylib's compile unit, and so
// the copy constructor is odr-used (giving it a real, callable,
// out-of-line function body instead of being folded away).
Resource r2(nullptr);
Resource r2_copy(r2);

extern "C" {
void plugin_init(void) {}

void plugin_entry(void) {}
}
