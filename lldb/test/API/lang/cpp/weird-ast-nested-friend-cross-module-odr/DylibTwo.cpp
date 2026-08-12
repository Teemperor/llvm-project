// See DylibOne.cpp/DylibOne.h for the conflicting definition of
// 'Outer::Inner'.
#include "DylibTwo.h"

Outer b_outer_storage;
Outer *b_outer = &b_outer_storage;

// Force 'Outer::make()' to actually be emitted into this dylib, instead
// of being discarded as an unused inline function.
Outer::Inner b_inner_storage = b_outer_storage.make();

extern "C" {
void dylib_two_init(void) {}
}
