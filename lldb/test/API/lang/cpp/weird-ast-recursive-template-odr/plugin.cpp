#include "plugin.h"

// See main.cpp: this 'Node<int>' has the same name and template argument as
// the one in main.cpp, but its body has an extra member ('tag') and is
// still self-referential through 'next'.
template <typename T> struct Node {
  Node<T> *next;
  int tag;
  T val;
};

// Build a one-node list inside the dylib, using the dylib's (conflicting)
// definition of Node<int>, and chain it onto the main executable's list so
// that a single expression has to walk across both conflicting
// specializations of the same self-referential template.
Node<int> plugin_head = {nullptr, 0xBAD, 1};

extern "C" {
void plugin_init(void *main_head_ptr) {
  // Wire plugin_head.next -> main_head, chaining this module's (conflicting)
  // Node<int> list onto the main executable's Node<int> list. 'next' is a
  // plain Node<int> * in both modules' definitions (same offset), so this
  // reinterpret through 'void *' is layout-compatible even though the two
  // 'Node<int>' definitions disagree on their full bodies.
  plugin_head.next = reinterpret_cast<Node<int> *>(main_head_ptr);
}

void plugin_entry() {}
}
