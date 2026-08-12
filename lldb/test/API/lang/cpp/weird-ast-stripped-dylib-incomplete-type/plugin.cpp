#include "plugin.h"

// The only complete definition of Handle in the whole program. This debug
// info is stripped from the dylib after linking (see Makefile's
// "strip -S" post-link step), so by the time LLDB tries to complete this
// type, this definition is gone from every module's debug info.
struct Handle {
  int id;
  const char *name;

  int getId() const { return id; }
};

Handle g_handle_storage = {42, "the-handle"};
Handle *g_handle = &g_handle_storage;

extern "C" {
void plugin_init(void) {
  // Touch g_handle so it (and its type) definitely end up used/emitted.
  g_handle->id = 42;
}

void plugin_entry(void) {}
}
