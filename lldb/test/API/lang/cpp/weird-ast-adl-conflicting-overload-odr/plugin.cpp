#include "plugin.h"

// See main.cpp for the full scenario description. This is the dylib side
// of the ODR violation: 'ns::Vec' has an extra 'int y' member (different
// layout from main.cpp's single-'int' 'Vec'), and 'ns::len' returns
// 'double' instead of 'int' for the same 'ns::Vec' parameter type.
namespace ns {
struct Vec {
  int x;
  int y;
};
double len(Vec v) { return v.x + v.y; }
} // namespace ns

ns::Vec vb{5, 6};

extern "C" {
void plugin_init() {}

void plugin_entry() {
  double result = len(vb);
  (void)result;
}
}
