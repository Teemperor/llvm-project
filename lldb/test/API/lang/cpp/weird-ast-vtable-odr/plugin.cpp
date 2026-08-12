#include "plugin.h"

// This is a DIFFERENT (ODR-violating) definition of 'Widget' compared to
// the one in main.cpp: here the class only has a single virtual method,
// but an extra non-virtual data member. This gives the class a different
// vtable shape (one fewer virtual slot) *and* a different overall layout
// than the exe's definition of the same-named class.
class Widget {
public:
  virtual int f() { return 100; }
  int extra = 42;
};

Widget *gPluginWidget = 0;

extern "C" {
void plugin_init() { gPluginWidget = new Widget; }

void plugin_entry() {}
}
