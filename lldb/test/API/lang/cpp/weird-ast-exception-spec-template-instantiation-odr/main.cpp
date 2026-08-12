#include "plugin.h"

// This is the exe's definition of 'Wrapper<int>'. 'release' has a
// dependent noexcept-specification, 'noexcept(sizeof(T) <= 4)', which
// evaluates to 'noexcept(true)' for 'T = int'. The dylib below (see
// plugin.cpp) defines a template with the exact same name, template
// argument ('int'), field layout, and mangled linkage name for
// 'release', but a *different* dependent noexcept-expression that
// evaluates to 'noexcept(false)' for the same 'T = int'. This is a
// genuine ODR violation confined entirely to the exception
// specification: both 'Wrapper<int>' specializations look identical to
// LLDB's name-based type matching (same qualified name 'Wrapper<int>',
// same layout, same mangled name '_ZN7WrapperIiE7releaseEv'), but Clang
// only actually computes and caches the "true" underlying
// FunctionProtoType exception spec lazily, via
// Sema::ResolveExceptionSpec, the first time it is actually needed
// (e.g. when the function is called, or 'noexcept(expr)' is evaluated).
// Since DWARFASTParserClang/ASTImporter fabricate the two conflicting
// ClassTemplateSpecializationDecls for 'Wrapper<int>' independently, from
// two different modules' DWARF, LLDB's per-target shared scratch
// ASTContext can end up trying to reconcile/merge two same-named
// CXXMethodDecls whose FunctionProtoTypes disagree only in their
// (lazily-resolved) exception specification.
template <typename T> struct Wrapper {
  void release() noexcept(sizeof(T) <= 4);
  int val;
};

template <typename T> void Wrapper<T>::release() noexcept(sizeof(T) <= 4) {}

// Force instantiation of 'Wrapper<int>' in the exe's debug info.
template struct Wrapper<int>;

Wrapper<int> g_main_wrapper = {111};

int main() {
  g_main_wrapper.release();
  plugin_init();
  plugin_entry();
  return 0;
}
