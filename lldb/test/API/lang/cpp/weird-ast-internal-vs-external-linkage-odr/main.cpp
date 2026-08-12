#include "plugin.h"

// See plugin.cpp: this 'Helper' has the exact same name and members as the
// dylib's 'Helper', but is wrapped in an anonymous namespace, giving it
// *internal* linkage. The dylib's 'Helper' has ordinary (external) linkage.
// This is a genuine ODR violation: two structurally-identical-looking types
// with the same unqualified name 'Helper' but different linkage, defined in
// different translation units/modules.
namespace {
struct Helper {
  int tag;
  void run();
};
} // namespace

void Helper::run() { tag = 100; }

Helper gHelper = {1};

// Force the compiler to keep gHelper (and Helper::run's real definition)
// around instead of optimizing them away as unused internal-linkage
// symbols.
extern "C" void *keep_alive_ptr = &gHelper;

int main() {
  gHelper.run();
  plugin_init();
  plugin_entry();
  return 0;
}
