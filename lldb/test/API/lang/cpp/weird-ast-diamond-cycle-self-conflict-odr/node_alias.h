// node_alias.h: intentionally uses its OWN include guard (NOT NODE_H_IN),
// so a translation unit that includes both "node.h" and "node_alias.h"
// (as plugin.cpp below does) ends up parsing *two* differently-shaped
// function-local 'struct Node' types, both spelled "Node" at the DWARF
// level, within the very same compile unit. This models a
// duplicated/drifted vendored copy of a header ending up included
// alongside the "real" one -- bad header hygiene that nonetheless compiles
// fine, since function-local classes with the same spelling in different
// functions are legitimately distinct C++ types.
#ifndef NODE_ALIAS_H_IN
#define NODE_ALIAS_H_IN

#include "base.h"

inline void *make_local_node_v2() {
  struct Node {
    Base *parent;
    short tag;
    // Perturbation: this second "Node" has an extra trailing field that
    // the first one (node.h) doesn't have.
    char extra_from_alias;
  };
  static Node storage;
  return &storage;
}

#endif // NODE_ALIAS_H_IN
