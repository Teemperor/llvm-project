#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a namespace alias called 'config' that points at a
// *different* underlying namespace ('impl_v1' here, 'impl_v2' in the dylib),
// and each of those underlying namespaces defines its own (differently
// sized) 'Config' struct. The qualified expression 'config::Config' is
// therefore lexically identical in both translation units but refers to two
// genuinely incompatible types depending on which alias target is in scope.
//
// This exercises NamespaceAliasDecl handling in the ASTImporter/Sema lookup
// path: resolving 'config::Config' requires dereferencing the alias's
// getNamespace() to find the aliased target namespace, and if LLDB's
// internal machinery ends up merging/unifying two same-named
// NamespaceAliasDecls that point at different targets (as opposed to two
// truly identical redeclarations of the same alias), the alias could start
// resolving to the wrong target namespace -- or worse, to a dangling one --
// regardless of which frame's expression context actually requested it.
namespace impl_v1 {
struct Config {
  int flags;
};
} // namespace impl_v1
namespace config = impl_v1;

config::Config ca{1};

int a_func() {
  int result = ca.flags;
  plugin_init();
  plugin_entry();
  return result;
}

int main() { return a_func(); }
