#include "right.h"

static Node gNode;

extern "C" {
void right_init(void) {
  gNode.parent = base_get();
  gNode.tag = 200;
}
void *right_get_node(void) { return &gNode; }
}
