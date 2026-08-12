// The main executable links against two dylibs that each define a
// mutually-incompatible 'struct Shape' under the same tag name:
//   dylib1: struct Shape { int tag; int value; };
//   dylib2: struct Shape { double w, h, d; };
//
// Evaluating expressions that reference both globals forces LLDB's
// DWARFASTParserClang/ASTImporter machinery to import both conflicting
// definitions of 'Shape' into the target's shared per-target scratch
// ASTContext at the same time (a real ODR violation).
#include "plugin.h"

extern "C" void plugin_entry() {}

int main() {
  dylib1_entry();
  dylib2_entry();
  plugin_entry();
  return 0;
}
