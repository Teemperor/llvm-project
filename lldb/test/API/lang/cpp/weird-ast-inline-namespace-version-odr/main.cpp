#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a namespace 'lib' with an *inline* namespace inside it
// ('v1' here, 'v2' in plugin.cpp), and each inline namespace defines a
// struct called 'Handle' with a completely different layout. Because the
// inline namespaces are transparent to name lookup, both definitions are
// reachable via the exact same qualified name 'lib::Handle' -- this is a
// real ODR violation (same qualified name resolves to incompatible types
// depending on which TU/module you ask), not just an overload/template
// difference.
//
// lib::Handle here: { int id; } (4 bytes)
// lib::Handle in plugin.cpp: { void *ptr; long tag; } (16 bytes)
namespace lib {
inline namespace v1 {
struct Handle {
  int id;
};
} // namespace v1
} // namespace lib

lib::Handle hA{1};

int main() {
  lib::Handle local{42};
  plugin_init();
  plugin_entry();
  return local.id;
}
