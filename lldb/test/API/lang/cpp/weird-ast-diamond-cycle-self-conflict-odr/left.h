#ifndef LEFT_H_IN
#define LEFT_H_IN

#include "base.h"

// Left privately redefines a helper 'struct Node' inside an anonymous
// namespace (so it has internal linkage and is *not* meant to be the same
// type as Right's or Top's "Node"). Left's 'tag' is an 'int'.
namespace {
struct Node {
  Base *parent;
  int tag;
};
} // namespace

extern "C" {
void left_init(void);
void *left_get_node(void);
}

#endif // LEFT_H_IN
