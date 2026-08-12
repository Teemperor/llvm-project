#include "plugin.h"

// See main.cpp: this 'Box' has the same name as main.cpp's 'Box', but its
// body has an extra leading field ('tag'), while 'Holder' is written
// identically in both modules. This module also instantiates
// 'Holder<Box, int>' - the exact same specialization main.cpp instantiates
// - but does so by expanding *this* module's (conflicting) 'Box' through
// the template-template argument.
template <typename T> struct Box {
  int tag;
  T val;
};

template <template <typename> class C, typename U> struct Holder {
  C<U> inner;
  int flag;
};

// Implicitly instantiates 'Holder<Box, int>' (and, transitively,
// 'Box<int>') using plugin.cpp's (conflicting) definition of 'Box'.
Holder<Box, int> plugin_holder = {{0xBAD, 222}, 2};

extern "C" {
void plugin_init(void) {}
void plugin_entry(void) {}
}
