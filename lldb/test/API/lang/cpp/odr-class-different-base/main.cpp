#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class called 'Shape', but with different base
// classes. This is an ODR violation across translation units (and here,
// across LLDB modules -- the executable and the dylib).
class BaseA {
public:
  int a = 1;
};

class Shape : public BaseA {
public:
  int shapeField = 2;
};

Shape global_shape;

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
