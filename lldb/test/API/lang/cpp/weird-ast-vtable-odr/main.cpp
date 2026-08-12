#include "plugin.h"

// This is the exe's definition of 'Widget': it has two virtual methods
// (a bigger vtable) and no extra data member, unlike the dylib's
// same-named 'Widget' class (see plugin.cpp). This is a deliberate ODR
// violation so that LLDB's type-system machinery (ASTImporter /
// TypeSystemClang / DWARFASTParserClang) has to reconcile two CXXRecordDecls
// for 'Widget' with different vtable shapes when both are referenced from
// the same expression.
class Widget {
public:
  virtual int f() { return 1; }
  virtual int g() { return 2; }
};

Widget global_widget;

int main() {
  // Make sure debug-info for this definition of 'Widget' is emitted in
  // the exe's compile unit.
  global_widget.f();

  plugin_init();
  plugin_entry();
  return 0;
}
