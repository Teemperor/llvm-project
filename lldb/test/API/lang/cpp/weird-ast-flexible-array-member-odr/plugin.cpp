#include "plugin.h"

// In the dylib, the same-named 'Buffer' struct instead has a concrete,
// fixed-size trailing array. This directly contradicts the main
// executable's definition, where the trailing array is an incomplete
// flexible array member (making the type itself have an "unknown" size
// in the strict sense). Merging the two same-named RecordDecls in the
// ASTImporter/TypeSystemClang machinery is a sizing contradiction that
// layout-computation code may not gracefully detect.
struct Buffer {
  int len;
  int data[8];
};

Buffer gFixedBuffer = {8, {1, 2, 3, 4, 5, 6, 7, 8}};

extern "C" {
void plugin_init(void) {}

void plugin_entry(void) {}
}
