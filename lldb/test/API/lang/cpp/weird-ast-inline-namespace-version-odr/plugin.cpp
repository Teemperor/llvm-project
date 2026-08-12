#include "plugin.h"

// See main.cpp: this 'lib::Handle' has the exact same qualified name as the
// one in main.cpp (both are reached transparently through an inline
// namespace, 'v1' there vs 'v2' here), but a completely different,
// incompatible layout:
//
// lib::Handle in main.cpp: { int id; } (4 bytes)
// lib::Handle here: { void *ptr; long tag; } (16 bytes)
namespace lib {
inline namespace v2 {
struct Handle {
  void *ptr;
  long tag;
};
} // namespace v2
} // namespace lib

lib::Handle hB{nullptr, 2};

extern "C" {
void plugin_init() {}

void plugin_entry() {
  lib::Handle local{nullptr, 99};
  (void)local;
}
}
