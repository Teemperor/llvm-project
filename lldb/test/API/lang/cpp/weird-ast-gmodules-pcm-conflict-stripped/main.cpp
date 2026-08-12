#include "plugin.h"

// Only ModuleA is on the include/module search path used for this
// translation unit (see Makefile), so this pulls in ModuleA's "struct
// Circle" (just a radius) and bakes its definition into the executable's
// own PCM (-fmodules -gmodules). The "plugin" dylib below is compiled
// against ModuleB's incompatible version of the very same "Shapes" module,
// where "Circle" additionally has an 'area' field.
#include "Shapes.h"

Circle gMainCircle;

int main() {
  gMainCircle = makeCircleA(1.0);

  plugin_init();
  plugin_entry();

  return 0;
}
