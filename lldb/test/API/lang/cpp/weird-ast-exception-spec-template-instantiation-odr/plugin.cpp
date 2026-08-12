#include "plugin.h"

// See main.cpp for the exe's definition of the same-named specialization.
// This module's 'Wrapper<int>' template pattern is textually almost
// identical to main.cpp's (same field layout, 'int val;', so DWARF gives
// both an identical qualified name 'Wrapper<int>' and both
// instantiations mangle 'release' to the exact same linkage name
// '_ZN7WrapperIiE7releaseEv'), but its noexcept specification is *not*
// actually the same predicate: main.cpp computes
// noexcept('sizeof(T) <= 4'), which is true for 'T = int', while this
// module computes noexcept('sizeof(T) <= 4 && sizeof(int) > 100'), which
// is false for 'T = int' (the extra conjunct is always-false, but not
// obviously so to a simple textual/name-based comparison of the
// specialization). The ODR violation is confined entirely to the
// dependent noexcept-expression that Sema::ResolveExceptionSpec lazily
// evaluates and caches per FunctionProtoType -- exactly the kind of
// place where LLDB's DWARFASTParserClang/ASTImporter, reconstructing two
// independent ClassTemplateSpecializationDecls named 'Wrapper<int>' from
// two different modules' DWARF post-hoc, could end up conflating the
// cached exception spec of one specialization's 'release' with the
// other's, e.g. via a type-identity cycle in ASTContext::getFunctionType
// (which folds the exception spec into the FunctionProtoType's
// FoldingSetNode key) or a stale Sema::ResolveExceptionSpec memoization.
template <typename T> struct Wrapper {
  void release() noexcept(sizeof(T) <= 4 && sizeof(int) > 100);
  int val;
};

template <typename T>
void Wrapper<T>::release() noexcept(sizeof(T) <= 4 && sizeof(int) > 100) {}

// Force instantiation of this (ODR-violating) 'Wrapper<int>' in the
// dylib's debug info.
template struct Wrapper<int>;

Wrapper<int> g_plugin_wrapper = {222};

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Break here. At this point neither module's 'Wrapper<int>' has been
  // imported into the target's shared scratch AST context yet.
  g_plugin_wrapper.release();
}
}
