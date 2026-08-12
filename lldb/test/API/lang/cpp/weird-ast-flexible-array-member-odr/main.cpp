#include "plugin.h"

#include <cstdlib>
#include <new>

// In the main executable 'Buffer' ends with a GNU flexible array member,
// so its "known" size only covers 'len' (the trailing array contributes
// nothing to sizeof in the strict sense).
struct Buffer {
  int len;
  int data[];
};

// Allocate extra trailing storage for the flexible array member via
// malloc + placement-new, so the flexible-array instance is actually
// usable at runtime.
static void *gBufferStorage = std::malloc(sizeof(Buffer) + 4 * sizeof(int));
Buffer *gFlexBuffer = new (gBufferStorage) Buffer{4};
static bool gFlexBufferInit = [] {
  for (int i = 0; i < 4; ++i)
    gFlexBuffer->data[i] = (i + 1) * 10;
  return true;
}();

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
