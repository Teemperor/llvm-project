#include "plugin.h"

// Deliberately not shared via a common header. Both this file and
// plugin.cpp define a C++20 concept named 'Addable' and a class template
// 'Adder' constrained by that concept, both instantiated with the exact
// same template argument (int). But the *wording* of the requires-clause
// differs between the two: this file's 'Addable' only requires operator+,
// while plugin.cpp's 'Addable' also requires operator-.
//
// This is a genuine ODR violation on the concept's constraint-expression
// (an AssociatedConstraint / RequiresExpr). Since DWARF has no
// representation for concepts or requires-clauses at all, the two
// specializations look byte-for-byte identical to LLDB's DWARF-based type
// reconstruction; the divergent constraint only exists in the compiler's
// (unobservable, via DWARF) AST for each TU. This test exists to probe
// whether the ASTImporter/DWARFASTParserClang machinery nonetheless trips
// over the concept machinery (e.g. while merging the two
// ClassTemplateDecls for 'Adder' into the shared scratch AST context) when
// both specializations are pulled into the same expression.
template <typename T>
concept Addable = requires(T a, T b) { a + b; };

template <Addable T> struct Adder {
  T lhs;
  T rhs;
};

Adder<int> main_adder = {1, 2};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
