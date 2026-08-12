#include "plugin.h"

// See main.cpp: this 'Outer::Inner' has the same name as the one in
// main.cpp, but has an extra member ('y').
struct Outer {
  struct Inner {
    int x;
    int y;
  };
};

Outer::Inner *gPluginInner = nullptr;

extern "C" {
void plugin_init() { gPluginInner = new Outer::Inner{2, 3}; }

void plugin_entry() {}
}
