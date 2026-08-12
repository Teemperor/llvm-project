#include "plugin.h"

// This definition of 'Reading' is only visible to main.cpp. The 'value'
// field is 'const', which makes 'Reading' (and anything that embeds it as
// a member) not trivially default-constructible and not copy-assignable:
// Clang implicitly deletes the copy-assignment operator for a class that
// has a const-qualified non-static data member. The byte layout/size of
// 'Reading' (a single int) is identical to the dylib's conflicting
// definition below, so this ODR violation is invisible to anything that
// only looks at size/offsets.
struct Reading {
  const int value;
};

// 'Sensor' nests a 'Reading' as a member, so the const-qualifier ODR
// difference on 'Reading::value' propagates into 'Sensor's own special
// member functions: main.cpp's 'Sensor' also loses its implicit
// copy-assignment operator (and default constructor), while the dylib's
// 'Sensor' (below) keeps them, even though both 'Sensor' definitions have
// identical size/layout.
struct Sensor {
  Reading reading;
  int id;
};

Sensor gMainSensor = {{111}, 1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
