#include "plugin.h"

// This definition of 'Tuple3' is only visible to plugin.cpp. It reuses the
// exact same template name and is instantiated with the exact same
// template-id ('Tuple3<int, int, int>') as main.cpp's 'Tuple3', but the
// pack expansion in the array bound is used differently here:
// 'sizeof...(Ts) + 1' instead of 'sizeof...(Ts)'. This means the *same*
// template-id gets a dependent array bound that folds to a different
// constant (4 instead of 3) depending on which module's template body is
// consulted, even though both declarations spell the template head
// identically ("template <typename... Ts> struct Tuple3 { ... }") at the
// AST/DWARF level. This is a genuine ODR violation: the identical
// specialization 'Tuple3<int, int, int>' has two incompatible definitions
// whose disagreement is baked into a dependent-sized array type's folded
// constant expression rather than just a differing member list.
template <typename... Ts> struct Tuple3 {
  char pad[sizeof...(Ts) + 1];
  int a, b, c;
};

Tuple3<int, int, int> gPluginTuple = {{}, 10, 20, 30};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}
