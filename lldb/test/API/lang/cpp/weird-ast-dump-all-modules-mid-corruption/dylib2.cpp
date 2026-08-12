#include "plugin.h"

// This dylib's 'Shared' is spelled identically to DylibOne's (same name,
// same single member 'a'), but 'a' is a 'float' here instead of an 'int'.
// Evaluating an expression that reads 'a' off *both* dylibs' 'Shared'
// pointers in the same statement (see the test) forces LLDB's
// ASTImporter/TypeSystemClang machinery to pull both conflicting
// completions of 'Shared' into the shared per-target scratch AST context
// at once, in order to compute a common type for the '+' operator.
struct Shared {
  float a;
};

Shared *gShared2 = nullptr;

extern "C" {

void dylib2_init() {
  static Shared s{2.0f};
  gShared2 = &s;
}

} // extern "C"
