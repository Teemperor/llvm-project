// The main executable and the 'plugin' dylib both define a function
// that ends up with the *exact same* linker-visible symbol name
// (_ZN4impl3runEv, the real Itanium mangling of "void impl::run()"),
// but with two genuinely different, ODR-violating C++ signatures:
//
//   main executable (this file): void impl::run()
//   plugin.cpp (dylib):          void impl::run(Args)   [see plugin.cpp]
//
// Both are declared __attribute__((weak)) so the linker doesn't reject
// the duplicate-but-differently-typed definition outright; it just
// silently picks one of the two weak definitions to satisfy every
// reference to that symbol name (including references from the
// *other* module). This means the debug info (DWARF) for the two
// modules disagrees about what type 'impl::run' has, while at runtime
// there is only a single merged function body.
//
// This is meant to stress SymbolFileDWARF's function-lookup-by-mangled-
// name and the ASTImporter's decl-merging-by-linkage-name: if either
// module is asked to resolve 'impl::run' by its DW_AT_linkage_name, it
// should get back a FunctionDecl with a *different* FunctionProtoType
// than the other module's FunctionDecl for the same linkage name.
#include "plugin.h"

namespace impl {
// Real (weak) definition: no arguments, returns void. Mangled name is
// _ZN4impl3runEv.
__attribute__((weak)) void run() {}
} // namespace impl

int main() {
  plugin_init();
  impl::run();
  plugin_entry();
  return 0;
}
