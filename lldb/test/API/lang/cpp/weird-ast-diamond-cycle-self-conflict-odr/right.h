#ifndef RIGHT_H_IN
#define RIGHT_H_IN

#include "base.h"

// Right also privately redefines a helper 'struct Node' inside its own
// anonymous namespace, but with 'tag' as a 'long' instead of Left's 'int'.
// Same spelling ("Node"), different layout: a genuine ODR violation once
// both Left's and Right's "Node" get pulled into the same expression.
namespace {
struct Node {
  Base *parent;
  long tag;
};
} // namespace

extern "C" {
void right_init(void);
void *right_get_node(void);
}

#endif // RIGHT_H_IN
