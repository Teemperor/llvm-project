#ifndef NODE_H_IN
#define NODE_H_IN

#include "base.h"

// Returns a pointer to a function-local 'struct Node'. Local classes are
// only nameable inside the function that defines them, but at the
// DWARF/debug-info level they still produce a CXXRecordDecl spelled "Node"
// -- textually identical in name to the one produced by node_alias.h
// below, but a genuinely distinct type (this models Top's "own" Node,
// reached via this header).
inline void *make_local_node_v1() {
  struct Node {
    Base *parent;
    short tag;
  };
  static Node storage;
  return &storage;
}

#endif // NODE_H_IN
