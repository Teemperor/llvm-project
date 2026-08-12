#include "plugin.h"

// This definition of 'Mode' is only visible to plugin.cpp. It reuses the
// same name and the same enumerator names as main.cpp's (conflicting)
// definition above, but its fixed underlying type is 'long long' (8
// bytes) instead of 'short' (2 bytes).
enum class Mode : long long { A, B, C };

Mode gPluginMode = Mode::C;
Mode *gPluginModePtr = &gPluginMode;

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}
