#include "plugin.h"

// See main.cpp: this dylib defines a same-named 'Secret<int>' class
// template specialization with the exact same shape (a private 'value'
// member and a user-declared constructor), but WITHOUT any friend
// declaration at all. This TU also defines its own unrelated 'struct
// Peek' (an empty-ish, unconnected type) just to keep the name 'Peek'
// present but semantically disconnected from Secret<int>'s (nonexistent,
// here) friend list.
//
// This is an ODR violation: two definitions of the class template
// specialization Secret<int> exist across the exe and dylib, one with a
// FriendDecl for 'Peek' threaded into its lexical/redecl chain (main.cpp),
// and one without any friends at all (this file). When LLDB's
// ASTImporter/TypeSystemClang machinery imports/merges these two
// CXXRecordDecls into the shared per-target scratch AST, the resulting
// decl's friend-list bookkeeping may end up inconsistent with what Sema's
// access-control machinery (and AST-dumping/traversal code) expects.
template <typename T> struct Secret {
private:
  T value;

public:
  Secret(T v) : value(v) {}
};

// Unrelated 'Peek' -- same name as the exe's, but no relationship to
// Secret<int> here.
struct Peek {
  int unrelated_field = 0;
};

Secret<int> plugin_secret(99);
Peek plugin_peek;

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Reference plugin_secret/plugin_peek so their debug info definitely
  // gets emitted. (Can't touch plugin_secret.value directly here since
  // it's private and plugin_entry is not a friend/member.)
  plugin_peek.unrelated_field += 1;
}
}
