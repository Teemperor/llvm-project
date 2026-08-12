#include "plugin.h"

// This definition of the variadic class template 'Tuple3' is only visible
// to main.cpp. Its member array 'pad' has a *dependent* array bound,
// 'sizeof...(Ts)', which Sema has to fold into a constant (3, for the
// instantiation below) when instantiating the specialization. The overall
// byte size (and the type of 'pad') therefore depends on how the compiler
// evaluates the pack-expansion-derived constant expression for this
// particular template-id, not just on the template's spelling.
template <typename... Ts> struct Tuple3 {
  char pad[sizeof...(Ts)];
  int a, b, c;
};

Tuple3<int, int, int> gMainTuple = {{}, 1, 2, 3};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
