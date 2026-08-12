#include "plugin.h"

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Breakpoint is set here. At this point LLDB's per-module AST for the
  // main executable already contains a complete, well-formed 'Point'
  // (parsed from DWARF). The test then evaluates malformed expressions at
  // this stopped location that re-declare 'Point' with a syntactically
  // broken body/trailing expression, trying to get Clang's error recovery
  // (RecoveryExpr / partially-built CXXRecordDecl) to leak a broken decl
  // into the target's shared, persistent scratch ASTContext.
  int dummy = 0;
  (void)dummy;
}
}
