#include "plugin.h"

// Class template with a non-type template parameter 'N' that is used
// directly as an array bound. main.cpp only ever sees this definition of
// 'FixedBuf', where 'data' comes before 'checksum'.
//
// See plugin.cpp for the conflicting twist: the dylib defines the exact
// same template, instantiated with the exact same non-type template
// argument ('FixedBuf<16>'), but with the two fields swapped. Because the
// non-type template argument (16) is identical, Clang's ODR/structural
// equivalence machinery sees two ClassTemplateSpecializationDecls for
// 'FixedBuf<16>' with identical template argument lists (and therefore
// identical mangled names) but different field order/offsets - a
// genuine ODR violation baked directly into a non-type template
// parameter rather than a type template parameter.
template <int N> struct FixedBuf {
  char data[N];
  int checksum;
};

FixedBuf<16> main_buf = {"main_data_here", 111};

int main() {
  plugin_init();
  plugin_entry();
  return main_buf.checksum;
}
