#include "plugin.h"

// Only ModuleB is on the include/module search path used for this
// translation unit (see Makefile), so this pulls in ModuleB's incompatible
// "struct Circle" (radius plus a baked-in 'area' field) and bakes its
// definition into this dylib's own, differently-hashed PCM
// (-fmodules -gmodules). This dylib's DWARF is stripped after linking (see
// Makefile's post-link 'strip -S' step), so by the time LLDB looks at this
// image, the *only* remaining trace of "Circle" anywhere near this dylib
// is the dangling reference to ModuleB's conflicting .pcm that clang baked
// into the (now-gone) debug info while compiling this file.
#include "Shapes.h"

Circle gPluginCircle;

extern "C" {
void plugin_init(void) { gPluginCircle = makeCircleB(2.0, 12.56); }

void plugin_entry(void) {}
}
