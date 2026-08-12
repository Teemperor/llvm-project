#include "plugin.h"

// This definition of 'Secret' is only visible to main.cpp. 'value' is a
// public data member here.
//
// The dylib (see plugin.cpp) defines a same-named, same-layout 'Secret'
// whose 'value' member is *private* instead. Both fields have identical
// name, type, and offset, and differ only in their 'AccessSpecifier' -- a
// genuine ODR violation ([class.mem]) that is completely invisible to
// anything that only looks at size/layout.
class Secret {
public:
  int value;
  int getValue() { return value; }
};

Secret secretFromMainExe = {42};

// See plugin.cpp: a second, related ODR conflict on the accessibility of
// a *base class* rather than a data member. main.cpp's 'Derived' inherits
// from 'Base' publicly; the dylib's same-named 'Derived' (see plugin.cpp)
// inherits from its own same-named, same-layout 'Base' privately instead.
struct Base {
  virtual ~Base() {}
  int value;
};

class Derived : public Base {
public:
  Derived(int v) { value = v; }
  virtual int getValue() { return value; }
};

Derived derivedFromMainExe(2);

int main() {
  plugin_init();
  plugin_entry(&secretFromMainExe);
  return 0;
}
