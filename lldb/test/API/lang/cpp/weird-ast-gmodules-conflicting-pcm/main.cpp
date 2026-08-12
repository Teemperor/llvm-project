#include "plugin.h"

// Only ModuleA is on the include path used for this translation unit (see
// Makefile), so this pulls in ModuleA's "struct Shared" and bakes its
// definition into the executable's own PCM (-fmodules -gmodules). ModuleB
// -- built into the dylib below -- independently defines an unrelated
// "struct Shared" with a different layout under the same name.
#include "Shared.h"

Shared gModuleAGlobal;

int main() {
  gModuleAGlobal = makeSharedFromModuleA(17);

  plugin_init();
  plugin_entry();

  return 0;
}
