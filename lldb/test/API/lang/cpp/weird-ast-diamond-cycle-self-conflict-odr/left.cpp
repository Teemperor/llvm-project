#include "left.h"

static Node gNode;

extern "C" {
void left_init(void) {
  gNode.parent = base_get();
  gNode.tag = 100;
}
void *left_get_node(void) { return &gNode; }
}
