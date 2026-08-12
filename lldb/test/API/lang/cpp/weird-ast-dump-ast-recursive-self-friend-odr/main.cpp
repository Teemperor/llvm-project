#include "plugin.h"

// The main executable's 'Node': a self-referential linked-list node with a
// (legal, no-op) self-friend declaration. 'friend struct Node;' inside
// 'Node' itself doesn't change the type's layout or semantics -- it's here
// purely to give DWARFASTParserClang/TypeSystemClang something slightly
// unusual to chew on while completing the RecordDecl for a type that also
// contains a pointer to itself.
//
// This 'Node' is a genuine ODR violation against the dylib's 'Node' (see
// plugin.cpp): same name, same field names and types, but reordered fields
// (and no friend declaration). Both are self-referential via 'next', so
// DWARF encodes the pointee type recursively in both modules.
struct Node {
  Node *next;
  friend struct Node;
  int data;
};

// A 3-node linked list in the main executable's global scope.
Node g_node3 = {nullptr, 3};
Node g_node2 = {&g_node3, 2};
Node g_node1 = {&g_node2, 1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
