// This is "Top": it links against Left and Right (see left.h/right.h) and
// forms the top of the diamond over Base. It holds a "Node*" obtained
// from each of Left and Right (two cross-dylib, mutually incompatible
// 'Node' types -- int tag vs long tag), *and* it defines its own third and
// fourth "Node" via two different header include paths (node.h directly,
// and node_alias.h, which is a drifted/duplicated header parsed under a
// different include guard). All four pointers get funnelled through a
// single 'TopState' so a single expression can walk all of them.
//
// This deliberately forces the same translation unit (this file) to
// contribute two structurally different DWARF "Node" RecordDecls of its
// own, on top of the two cross-dylib "Node" variants -- exercising
// self-import merging within one module's own DWARF-derived AST, in
// addition to the regular cross-module ODR conflict.
#include "plugin.h"

#include "base.h"
#include "node.h"
#include "node_alias.h"

extern "C" {
void left_init(void);
void right_init(void);
void *left_get_node(void);
void *right_get_node(void);
}

struct TopState {
  void *leftNode;
  void *rightNode;
  void *localNodeV1;
  void *localNodeV2;
};

static TopState gTop;

extern "C" {
void plugin_init(void) {
  left_init();
  right_init();
  gTop.leftNode = left_get_node();
  gTop.rightNode = right_get_node();
  gTop.localNodeV1 = make_local_node_v1();
  gTop.localNodeV2 = make_local_node_v2();
}

void plugin_entry(void) {
  // By the time we get here every flavor of "Node" (Left's, Right's, and
  // Top's own two locally-defined ones) has been constructed.
}
}
