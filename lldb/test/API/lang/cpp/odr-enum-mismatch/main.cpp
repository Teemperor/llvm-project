#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define an enum called 'Color' with the same enumerators, but
// different underlying values. This is an ODR violation across translation
// units (and here, across LLDB modules -- the executable and the dylib).
enum Color { Red = 0, Green = 1, Blue = 2 };

Color global_color = Green;

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
