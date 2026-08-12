#include "plugin.h"

// This is the exe's definition of 'Combo': it has a single virtual method
// ('f') and two narrow bitfields ('x' and 'y', 5 bits each). The dylib
// defines a same-named 'Combo' (see plugin.cpp) with a DIFFERENT vtable
// shape (an extra virtual method 'g') AND a DIFFERENT bitfield layout for
// 'x' (20 bits, no 'y' at all). This deliberately combines two ODR
// conflict dimensions - vtable shape and bitfield packing - in the very
// same RecordDecl, so that when LLDB's ASTImporter merges/reconciles the
// two conflicting CXXRecordDecls for 'Combo' in the shared scratch AST
// context, both Clang's ItaniumVTableBuilder *and* its bitfield-packing
// RecordLayoutBuilder logic have to operate on the resulting
// Frankenstein decl at once.
class Combo {
public:
  virtual int f() { return 1; }
  unsigned x : 5;
  unsigned y : 5;

  Combo() : x(1), y(2) {}
};

Combo global_combo;

int main() {
  // Make sure debug-info for this definition of 'Combo' is emitted in
  // the exe's compile unit.
  global_combo.x = 3;

  plugin_init();
  plugin_entry();
  return 0;
}
