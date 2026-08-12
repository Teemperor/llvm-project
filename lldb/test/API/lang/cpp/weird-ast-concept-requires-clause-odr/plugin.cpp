#include "plugin.h"

// See main.cpp: this 'Addable' concept has the same name as the one in
// main.cpp, but its requires-clause additionally requires operator-. The
// 'Adder<int>' class template constrained by this concept is otherwise
// identical to the one in main.cpp (same members, same name, same
// template argument), so DWARF reports what looks like "the same type" --
// but the underlying (DWARF-invisible) AssociatedConstraint / RequiresExpr
// attached to the template differs between the two translation units.
template <typename T>
concept Addable = requires(T a, T b) {
  a + b;
  a - b;
};

template <Addable T> struct Adder {
  T lhs;
  T rhs;
};

Adder<int> *gPluginAdder = nullptr;

extern "C" {
void plugin_init() { gPluginAdder = new Adder<int>{10, 20}; }

void plugin_entry() {}
}
