#include "plugin.h"

// See main.cpp: this 'Box<int>' has the same name and template argument as
// the one in main.cpp, but its body has an extra member ('extra'), and this
// TU's 'IntBox' alias names *this* (structurally different) instantiation.
template <typename T> struct Box {
  T val;
  T extra;
};
using IntBox = Box<int>;

IntBox ib2{1, 2};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}
