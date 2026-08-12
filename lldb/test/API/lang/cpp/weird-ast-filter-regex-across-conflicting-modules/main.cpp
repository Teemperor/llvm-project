#include "plugin.h"

// Main links all four dylibs. It never sees any of the four conflicting
// 'struct Config' definitions itself: it only stores four opaque
// 'void *' pointers, one per dylib, obtained from each dylib's
// dylib*_init(). Those pointers get reinterpreted as 'Config *' only
// later, inside test expressions, which is what forces LLDB's
// DWARFASTParserClang/ASTImporter machinery to import each dylib's own
// (mutually incompatible) 'Config' completion into the shared per-target
// scratch AST context.
void *g1 = nullptr;
void *g2 = nullptr;
void *g3 = nullptr;
void *g4 = nullptr;

void main_entry() {}

int main() {
  g1 = dylib1_init();
  g2 = dylib2_init();
  g3 = dylib3_init();
  g4 = dylib4_init();
  main_entry();
  return 0;
}
