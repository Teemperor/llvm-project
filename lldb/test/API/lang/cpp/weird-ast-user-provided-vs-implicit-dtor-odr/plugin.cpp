#include "plugin.h"

// This is a DIFFERENT (ODR-violating) definition of 'Logger' compared to
// the one in main.cpp: here there is no user-declared destructor at all,
// so 'Logger' gets an implicit, trivial destructor. The field layout
// ('int id;') is otherwise identical to the exe's 'Logger'.
//
// Evaluating an expression that forces LLDB to complete/instantiate this
// dylib's 'Logger' (specifically, to decide hasTrivialDestructor() and
// synthesize its implicit destructor) *after* the exe's conflicting,
// user-provided, non-trivial, out-of-line destructor has already been
// attached to the 'same' canonical merged RecordDecl in the shared
// per-target scratch AST context forces
// TypeSystemClang::CompleteTagDeclarationDefinition to be invoked a
// second time on a Decl that Clang believes is already completely and
// consistently defined.
struct Logger {
  int id;
};

Logger plugin_logger{42};

extern "C" {
void plugin_init() {}

void plugin_entry() { plugin_logger.id = 43; }
}
