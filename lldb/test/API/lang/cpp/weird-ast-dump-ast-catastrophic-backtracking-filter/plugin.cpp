#include "plugin.h"

// A single class template specialization, independent of the 40
// 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac<N>' classes the test populates the
// scratch AST with via 'expr'. Manual exploration (see the test's
// docstring) found that 'target modules dump ast --filter <anything
// non-empty>' reliably segfaults LLDB via
// 'clang::RecursiveASTVisitor<ASTPrinter>::
// TraverseClassTemplateSpecializationDecl' as soon as ANY
// 'ClassTemplateSpecializationDecl' is reachable from the translation
// unit being dumped - completely independent of whether the filter
// string looks like a "pathological" regex, a plain identifier, or
// garbage that matches nothing. This one specialization ('Wrap<int>') is
// enough to make that crash reachable; the 40 non-template classes below
// are additionally there to populate the scratch AST with a large number
// of candidate decl names (as the scenario intends), even though the
// filter here is a plain substring search, not a backtracking regex
// engine (see the module docstring).
template <typename T> struct Wrap { T val; };
Wrap<int> g_wrap;

extern "C" void plugin_init(void) {}

extern "C" void plugin_entry(void) {
  // breakpoint here
  int x = 1;
  (void)x;
}
