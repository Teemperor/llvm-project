#include "plugin.h"

// This is the main executable's definition of 'class Box': 'secret' is a
// single private int, and a free function 'peek' is declared a friend so
// it can reach into it. The FriendDecl for 'peek' is injected into the
// enclosing (translation-unit) DeclContext via Sema's friend-declaration
// machinery -- a mechanism that is distinct from (and threaded alongside)
// Box's own member list.
//
// Note: deliberately not shared via a common header. plugin.cpp defines
// its own same-named 'class Box' with a completely different layout
// (a double + a long instead of a single int) and its own same-named
// friend function 'peek', but operating on that incompatible layout. This
// is an ODR violation across LLDB "modules" (the executable and the
// dylib): the two 'Box' types share a name and a friend-injected 'peek',
// but have incompatible in-memory representations.
class Box {
  int secret = 1;
  friend int peek(Box &);
};

int peek(Box &b) { return b.secret; }

Box main_box;

int main() {
  // Force emission/use of peek(Box&) so debug info for Box and its
  // FriendDecl-injected friend function definitely exists in the exe.
  int v = peek(main_box);
  v += 0;

  plugin_init();
  plugin_entry();
  return 0;
}
