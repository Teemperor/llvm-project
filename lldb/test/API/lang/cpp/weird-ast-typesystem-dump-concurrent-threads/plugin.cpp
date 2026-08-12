#include "plugin.h"

// A moderately-shaped type (a couple of fields plus a member function)
// gives 'target dump typesystem' and 'target modules dump ast' some real
// tree structure to walk (via clang::RecursiveASTVisitor) while another
// thread is concurrently forcing new, ODR-conflicting 'MyInt' typedef
// declarations into the very same shared, per-target scratch
// TypeSystemClang/ASTContext via back-to-back 'expression' commands.
struct Shape {
  double a;
  double b;
  double area() const { return a * b; }
};

Shape gShape{2.0, 3.0};

void plugin_init(void) {}

// Breakpoint location: by the time we stop here, gShape's debug info is
// available and the dylib is fully loaded.
void plugin_entry(void) {}
