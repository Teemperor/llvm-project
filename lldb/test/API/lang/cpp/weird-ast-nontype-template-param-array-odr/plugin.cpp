#include "plugin.h"

// Same primary template declaration as in main.cpp (deliberately not
// shared via a common header - each module builds its own, independent
// notion of what 'FixedBuf' looks like), except the field order is
// swapped: 'checksum' comes before the array 'data' here.
//
// 'FixedBuf<16>' is instantiated with the exact same non-type template
// argument (16) as main.cpp's 'FixedBuf<16>', so both
// ClassTemplateSpecializationDecls describe the "same" specialization
// as far as the template argument list / mangled name is concerned, but
// they disagree about field order (and therefore field offsets) inside
// the record. This is a real ODR violation that happens to be encoded
// entirely through a non-type template parameter baked into an array
// bound, rather than through differing type arguments.
template <int N> struct FixedBuf {
  int checksum;
  char data[N];
};

FixedBuf<16> plugin_buf = {222, "plugin_data"};

extern "C" {
void plugin_init(void) {}

void plugin_entry(void) {}
}
