#include "plugin.h"

// This is the exe's definition of 'Secret<int>': 'value' is private, but
// 'struct Peek' is declared a friend, so Peek::get() can reach into it.
// Instantiating Secret<int> here (via 'main_secret') and calling
// Peek::get() forces debug info for the FriendDecl (threaded into
// Secret<int>'s lexical/redecl chain) to be emitted in the exe's compile
// unit.
//
// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template specialization called 'Secret<int>'
// and a class called 'Peek', but plugin.cpp's 'Secret<int>' has NO friend
// declaration at all, and its 'Peek' is a completely unrelated, unconnected
// type. This is an ODR violation across LLDB modules (the executable and
// the dylib): one 'Secret<int>' carries a FriendDecl for 'Peek' in its AST,
// the other does not.
template <typename T> struct Secret {
private:
  T value;
  friend struct Peek;

public:
  Secret(T v) : value(v) {}
};

struct Peek {
  static int &get(Secret<int> &s) { return s.value; }
};

Secret<int> main_secret(42);

int main() {
  // Force emission/use of Peek::get so debug info for Secret<int> and Peek
  // (and the FriendDecl linking them) definitely exists in the exe.
  int &ref = Peek::get(main_secret);
  ref = ref + 0;

  plugin_init();
  plugin_entry();
  return 0;
}
