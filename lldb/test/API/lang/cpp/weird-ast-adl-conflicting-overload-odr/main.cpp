#include "plugin.h"

// The main executable and plugin.cpp each define a namespace 'ns' with a
// struct 'Vec' and a free function 'len' found via argument-dependent
// lookup (ADL) on 'Vec's associated namespace. The two definitions are a
// real ODR violation: not only does 'Vec' have a different layout (one
// 'int' member here, two in plugin.cpp), but 'ns::len' also has a
// different *return type* for the exact same (mangled) parameter type,
// 'ns::Vec' ('int len(Vec)' here vs 'double len(Vec)' in plugin.cpp).
//
// Calling the unqualified 'len(some_vec)' from the expression evaluator
// relies on ADL to find 'ns::len' via 'ns::Vec's enclosing namespace. If
// the per-target shared scratch AST context ends up with both modules'
// conflicting 'ns' namespaces/'len' overloads visible at once, Sema's
// overload resolution has two viable, non-template candidates that are
// only distinguished by return type (which participates in overload
// resolution not at all for a plain call) -- this is exactly the kind of
// state that could trip an "exactly one best viable candidate" invariant
// in OverloadCandidateSet resolution, or cause the JIT to invoke the
// wrong-ABI (int vs double return) mangled symbol for the call.
namespace ns {
struct Vec {
  int x;
};
int len(Vec v) { return v.x; }
} // namespace ns

ns::Vec va{5};

int main() {
  plugin_init();
  plugin_entry();
  int result = len(va);
  return result;
}
