#include "plugin.h"

// This definition of 'Pod' is only visible to plugin.cpp. Unlike the
// conflicting 'Pod' in main.cpp, this one has no user-declared constructors
// at all, so it is a plain aggregate with an implicit, trivial, non-deleted
// default constructor.
struct Pod {
  int a;
  int b;
};

Pod gPluginPod{3, 4};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}
