#include "plugin.h"

// Same 50-level-deep 'Wrap<Wrap<...<int>...>>' template instantiation as
// main.cpp, defined independently in the dylib's own translation unit so
// that the dylib's debug info also contains a DWARF DIE tree for the same
// (structurally identical) deeply-nested type. Pulling both modules'
// versions of 'DeepType' into an expression forces LLDB's
// DWARFASTParserClang/ASTImporter to parse -- and potentially reconcile --
// two independently-parsed copies of this extremely deep template
// instantiation within the shared per-target scratch AST context.
template <typename T> struct Wrap { T val; };

using DeepType =
    Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap
    <Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap
    <Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap
    <Wrap<Wrap<Wrap<Wrap<Wrap<int>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;

DeepType *g_deep_from_plugin;

extern "C" {
void plugin_init(void *deep_ptr) {
  // Unused; passed in so main.cpp's DeepType instantiation is reachable
  // from an expression evaluated inside the dylib.
  (void)deep_ptr;
  g_deep_from_plugin = new DeepType();
}

void plugin_entry() {}
}
