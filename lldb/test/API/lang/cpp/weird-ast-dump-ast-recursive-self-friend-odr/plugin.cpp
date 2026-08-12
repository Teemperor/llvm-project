#include "plugin.h"

// The dylib's 'Node': same name and same field names/types as main.cpp's
// 'Node', but with the fields reordered ('data' before 'next') and no
// friend declaration. This is a genuine ODR violation across the module
// boundary: the same type name resolves to two structurally distinct,
// independently-completed RecordDecls. Like main.cpp's version, this
// 'Node' is self-referential via 'next'.
struct Node {
  int data;
  Node *next;
};

// A separate 2-node linked list in the dylib's global scope, using the
// reordered layout.
Node g_dylib_node2 = {20, nullptr};
Node g_dylib_node1 = {10, &g_dylib_node2};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}
