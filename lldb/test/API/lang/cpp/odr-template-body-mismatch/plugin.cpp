#include "plugin.h"

// See main.cpp: this 'Box<int>' has the same name and template argument as
// the one in main.cpp, but its body has an extra member ('extra').
template <typename T> struct Box {
  T value;
  T extra;
};

Box<int> *gPluginBox = nullptr;

extern "C" {
void plugin_init() { gPluginBox = new Box<int>{2, 3}; }

void plugin_entry() {}
}
