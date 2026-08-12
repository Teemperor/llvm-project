#include "plugin.h"

// Recursive case: byte-for-byte identical to main.cpp's.
template <typename Head, typename... Tail> struct Cons {
  Head head;
  Cons<Tail...> *rest;
};

// Base case: DELIBERATELY different from main.cpp's. An extra 'sentinel'
// field is squeezed in before 'rest', changing the layout of the base
// case's specialization ('Cons<int>') while the recursive case
// ('Cons<int,int,int>', 'Cons<int,int>') stays textually identical to
// main.cpp's. This means completing 'Cons<int,int,int>' forces the
// ASTImporter to walk down through the (identical) 'Cons<int,int>' into
// the conflicting base case 'Cons<int>' - the ODR conflict is only reached
// at the bottom of a chain of recursively-dependent instantiations rather
// than at the top of a self-referential type.
template <typename Head> struct Cons<Head> {
  Head head;
  int sentinel;
  void *rest;
};

// Build a Cons<int,int,int> list entirely inside the dylib, using the
// dylib's (conflicting) definition of the base case 'Cons<int>'.
Cons<int, int, int> plugin_list;

extern "C" {
void plugin_init(void *main_list_ptr) {
  plugin_list.head = 10;
  plugin_list.rest = new Cons<int, int>();
  plugin_list.rest->head = 20;
  plugin_list.rest->rest = new Cons<int>();
  plugin_list.rest->rest->head = 30;
  plugin_list.rest->rest->sentinel = 0xBAD;
  // Wire the dylib's innermost (conflicting-layout) base-case node's
  // 'rest' across the module boundary onto the main executable's
  // top-level list, so that a single expression evaluated in the dylib's
  // context has to walk from the dylib's conflicting 'Cons<int>' back
  // into the main executable's (non-conflicting) 'Cons<int,int,int>'.
  // 'rest' is a plain 'void *' in both modules' base-case definitions
  // (same offset relative to the field itself, even though the base
  // case's overall layout differs due to 'sentinel'), so this
  // reinterpret through 'void *' is layout-compatible for the pointer
  // itself even though the two 'Cons<int>' definitions disagree on their
  // full bodies.
  plugin_list.rest->rest->rest = main_list_ptr;
}

void plugin_entry() {}
}
