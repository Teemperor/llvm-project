#include "plugin.h"

// This definition of 'Reading' is only visible to plugin.cpp. It reuses
// the same struct name and the same single-int layout as main.cpp's
// 'Reading', but 'value' is a plain (non-const) int here, so this
// 'Reading' stays trivially default-constructible and copy-assignable,
// unlike main.cpp's conflicting definition above.
struct Reading {
  int value;
};

// Same-named 'Sensor' as in main.cpp, with an identical byte layout, but
// because this module's 'Reading' has no const member, this 'Sensor' also
// keeps its implicit default constructor and copy-assignment operator.
struct Sensor {
  Reading reading;
  int id;
};

Sensor gPluginSensor = {{222}, 2};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}
