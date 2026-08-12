#include "plugin.h"

// See main.cpp: this dylib defines its own namespace alias also called
// 'config', but pointing at a different underlying namespace ('impl_v2')
// whose 'Config' struct has an extra member ('extra_flags') compared to
// main.cpp's 'impl_v1::Config'. The qualified name 'config::Config' is
// spelled identically in both translation units but is a genuine ODR
// violation: same qualified name, incompatible underlying types.
namespace impl_v2 {
struct Config {
  int flags;
  int extra_flags;
};
} // namespace impl_v2
namespace config = impl_v2;

config::Config cb{1, 2};

extern "C" {
void plugin_init() {}

void plugin_entry() {
  int result = cb.flags + cb.extra_flags;
  (void)result;
}
}
