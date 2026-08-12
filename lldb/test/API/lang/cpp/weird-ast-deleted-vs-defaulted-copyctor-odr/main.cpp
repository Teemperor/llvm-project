#include "plugin.h"

// This is the exe's definition of 'Widget': the copy constructor and copy
// assignment operator are both explicitly deleted. This is a deliberate
// ODR violation against the dylib's same-named 'Widget' (see plugin.cpp),
// whose copy constructor and copy assignment operator are explicitly
// defaulted (i.e. NOT deleted) instead.
//
// The intent is to see whether LLDB's type-system machinery (ASTImporter /
// TypeSystemClang / DWARFASTParserClang) can end up with a merged
// CXXRecordDecl for 'Widget' in the per-target shared scratch AST context
// where the special member FunctionDecls have inconsistent/conflicting
// isDeleted()/isDefaulted() bits -- which Clang's Sema and CodeGen
// generally assume can't happen once a canonical decl's deleted-ness has
// been fixed.
struct Widget {
  int x;
  Widget() : x(1) {}
  Widget(const Widget &) = delete;
  Widget &operator=(const Widget &) = delete;
};

// Global so debug-info for this definition of 'Widget' (including its
// deleted special members) is emitted with a full definition in the exe's
// compile unit.
Widget w1;

int main() {
  w1.x = 2;

  plugin_init();
  plugin_entry();
  return 0;
}
