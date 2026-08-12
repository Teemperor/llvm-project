#include "plugin.h"

// This definition of 'Mode' is only visible to main.cpp. It has the same
// name and the same enumerator names as the dylib's (conflicting)
// definition below, but its fixed underlying type is 'short' (2 bytes)
// instead of 'long long' (8 bytes). This is a very literal ODR violation:
// the bit-width of the type itself differs between the two definitions,
// not just its values.
enum class Mode : short { A, B, C };

Mode gMainMode = Mode::B;

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
