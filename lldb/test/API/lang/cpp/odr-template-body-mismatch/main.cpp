#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template called 'Box' instantiated with the
// exact same template argument (int), but the template bodies differ (the
// dylib's version has an extra member). This is a genuine ODR violation:
// unlike using different template arguments (which naturally produces
// distinct types), here the *same* specialization 'Box<int>' has two
// incompatible definitions.
template <typename T> struct Box {
  T value;
};

Box<int> global_box = {1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
