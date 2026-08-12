// This dylib defines 'Outer' with a nested 'class Inner' that befriends
// its enclosing class (a FriendDecl attached to Inner's DeclContext,
// granting Outer access to Inner's private 'secret' field). See
// DylibTwo.cpp/DylibTwo.h for a same-named, differently-shaped 'Outer':
// its nested 'Inner' is a 'struct' (not a 'class'), has no friend
// declaration of 'Outer', and carries an extra 'double' field. This is an
// ODR violation: the qualified name 'Outer::Inner' refers to two
// structurally incompatible class definitions (different tag-kind,
// different friend graph, different layout) across the two dylibs.
#include "DylibOne.h"

Outer a_outer_storage;
Outer *a_outer = &a_outer_storage;

// Force 'Outer::make()' (and, transitively, 'Outer::Inner's constructor)
// to actually be emitted into this dylib, instead of being discarded as
// an unused inline function.
Outer::Inner a_inner_storage = a_outer_storage.make();

extern "C" {
void dylib_one_init(void) {}
}
