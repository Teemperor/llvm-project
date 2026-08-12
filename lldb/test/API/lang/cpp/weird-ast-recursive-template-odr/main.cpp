#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a self-referential class template called 'Node',
// instantiated with the exact same template argument (int), but the two
// template bodies differ: the dylib's version (see plugin.cpp) has an
// extra member ('tag') squeezed in between the recursive pointer and the
// payload. This is a genuine ODR violation on the *same* specialization
// 'Node<int>', not just two different specializations.
//
// Both definitions are self-referential ('next' points back to 'Node<int>'
// itself), which means that when the ASTImporter tries to reconcile the two
// conflicting definitions of 'Node<int>' it may end up trying to import the
// 'next' member's pointee type, i.e. 'Node<int>' itself, again - risking
// unbounded recursion (and a stack overflow) instead of a clean ODR/layout
// error.
template <typename T> struct Node {
  Node<T> *next;
  T val;
};

// Build a little 2-node list entirely inside the main executable using
// main.cpp's definition of Node<int>.
Node<int> main_tail = {nullptr, 3};
Node<int> main_head = {&main_tail, 2};

int main() {
  plugin_init(&main_head);
  plugin_entry();
  return 0;
}
