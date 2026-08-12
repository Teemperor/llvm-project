#include "plugin.h"

// A plain, well-formed 'Point' whose debug info gets parsed into LLDB's
// per-module Clang AST (via DWARFASTParserClang). The test's expressions
// deliberately declare a *second*, syntactically broken 'Point' at the
// debugger prompt, forcing Clang's Sema error-recovery machinery to run
// while a same-named, already-complete 'Point' is in scope.
struct Point {
  int x, y;
};

int main() {
  plugin_init();
  Point p{1, 2};
  (void)p;
  plugin_entry();
  return 0;
}
