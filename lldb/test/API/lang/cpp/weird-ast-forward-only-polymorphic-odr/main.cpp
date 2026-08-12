#include "plugin.h"

// Forward declaration only. This translation unit never sees a complete
// definition of 'Opaque' - the only complete definition in the whole
// program lives in plugin.cpp (compiled into the dylib).
class Opaque;

// Global pointer to the (from this TU's point of view) incomplete type.
// Initialized via the dylib so that debug-info for this global exists in
// the main executable, but its pointee type is never completed here.
Opaque *gOpaquePtr = nullptr;

int main() {
  gOpaquePtr = static_cast<Opaque *>(make_opaque());
  plugin_init();
  plugin_entry();
  return 0;
}
