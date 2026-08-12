#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template called 'Box' instantiated with the
// exact same template argument (int), and both introduce a type alias
// 'IntBox' naming that specialization. This is a genuine ODR violation:
// the alias 'IntBox' names structurally different 'Box<int>' instantiations
// depending on which translation unit you look at (this one only has
// 'val', while plugin.cpp's 'Box<int>' also has 'extra').
template <typename T> struct Box {
  T val;
};
using IntBox = Box<int>;

IntBox ib{42};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
