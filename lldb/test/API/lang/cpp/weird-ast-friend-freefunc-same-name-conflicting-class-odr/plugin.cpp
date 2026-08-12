#include "plugin.h"

// See main.cpp: this dylib defines a same-named 'class Matrix' but
// representing a 3x3 matrix (a 9-int 'data' array instead of the exe's
// 4-int 2x2 layout), and its own friend-injected free function 'trace'
// with the identical declared signature 'int trace(const Matrix&)'
// operating on that incompatible, larger layout.
//
// This is an ODR violation: two definitions of 'class Matrix' exist
// across the exe and dylib, each with its own FriendDecl-injected
// 'trace' free function threaded into the translation-unit DeclContext,
// but the two 'Matrix' types are not layout-compatible at all -- yet
// both friend functions have byte-for-byte identical declared
// signatures. When LLDB's ASTImporter/TypeSystemClang/DWARFASTParserClang
// machinery imports and merges these conflicting CXXRecordDecls (and
// their friend-injected 'trace' FunctionDecls) into the shared
// per-target scratch AST context, the resulting bookkeeping for the
// friend/redeclaration lookup tables may end up inconsistent.
class Matrix {
  int data[9];
  friend int trace(const Matrix &);

public:
  Matrix(int a, int b, int c, int d, int e, int f, int g, int h, int i)
      : data{a, b, c, d, e, f, g, h, i} {}
};

int trace(const Matrix &m) { return m.data[0] + m.data[4] + m.data[8]; }

// data = {1, ..., 9} -> trace(mb) == data[0]+data[4]+data[8] == 1+5+9 == 15
Matrix mb(1, 2, 3, 4, 5, 6, 7, 8, 9);

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Force emission/use of trace(const Matrix&) so debug info for Matrix
  // and its FriendDecl-injected friend function definitely exists in the
  // dylib.
  int v = trace(mb);
  v += 0;
}
}
