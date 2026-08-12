#include "plugin.h"

// See main.cpp: this 'Color' has the same name and enumerators as the one
// in main.cpp, but the enumerators have different underlying values.
enum Color { Red = 10, Green = 11, Blue = 12 };

Color *gPluginColor = nullptr;

extern "C" {
void plugin_init() {
  static Color color = Green;
  gPluginColor = &color;
}

void plugin_entry() {}
}
