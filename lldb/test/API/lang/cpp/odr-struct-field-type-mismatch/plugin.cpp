#include "plugin.h"

// See main.cpp: this 'Point' has the same name as the one in main.cpp, but
// its 'x' field has a different type (float instead of int).
struct Point {
  float x;
};

Point *gPluginPoint = nullptr;

extern "C" {
void plugin_init() { gPluginPoint = new Point{2.5f}; }

void plugin_entry() {}
}
