#include "plugin.h"

// Only ModuleB is on the include path used for this translation unit (see
// Makefile), so this pulls in ModuleB's "struct Shared" and bakes its
// definition into this dylib's own PCM (-fmodules -gmodules).
#include "Shared.h"

Shared gModuleBGlobal;

extern "C" {
void plugin_init(void) { gModuleBGlobal = makeSharedFromModuleB(2.5); }

void plugin_entry(void) {}
}
