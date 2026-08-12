#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a type called 'Point', but with a field of a different
// type. This is an ODR violation: the definitions disagree across
// translation units (and here, across LLDB modules -- the executable and
// the dylib).
struct Point {
  int x;
};

Point global_point = {1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
