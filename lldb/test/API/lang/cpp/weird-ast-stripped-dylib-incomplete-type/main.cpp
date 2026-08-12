#include "plugin.h"

// The main executable only ever sees a forward declaration of Handle. The
// only complete definition of 'struct Handle' lives in the dylib's debug
// info, and that debug info gets stripped after linking (see Makefile).
// This means that, at debug time, no LLDB module has a complete definition
// of Handle anywhere.
struct Handle;

// Defined and initialized inside the dylib (plugin_init).
extern Handle *g_handle;

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
