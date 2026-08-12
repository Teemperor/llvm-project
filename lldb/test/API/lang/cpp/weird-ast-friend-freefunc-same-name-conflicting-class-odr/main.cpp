#include "plugin.h"

// This is the main executable's definition of 'class Matrix': a 2x2
// matrix represented as a flat 4-int array, with a free function 'trace'
// declared a friend so it can reach into the private 'data' member. The
// FriendDecl for 'trace' is injected into the enclosing (translation
// unit) DeclContext via Sema's friend-declaration machinery, distinct
// from (and threaded alongside) Matrix's own member list.
//
// Note: deliberately not shared via a common header. plugin.cpp defines
// its own same-named 'class Matrix' representing a 3x3 matrix (a 9-int
// array instead of 4), with its own same-named friend function 'trace'
// that has the exact same declared signature 'int trace(const Matrix&)'
// but computes over the incompatible, larger layout. This is an ODR
// violation across LLDB "modules" (the executable and the dylib): the
// two 'Matrix' types share a name and a friend-injected 'trace' with
// byte-for-byte identical signatures, but are not layout-compatible at
// all.
class Matrix {
  int data[4];
  friend int trace(const Matrix &);

public:
  Matrix(int a, int b, int c, int d) : data{a, b, c, d} {}
};

int trace(const Matrix &m) { return m.data[0] + m.data[3]; }

// data = {1, 2, 3, 4} -> trace(ma) == data[0] + data[3] == 1 + 4 == 5
Matrix ma(1, 2, 3, 4);

int main() {
  // Force emission/use of trace(const Matrix&) so debug info for Matrix
  // and its FriendDecl-injected friend function definitely exists in the
  // exe.
  int v = trace(ma);
  v += 0;

  plugin_init();
  plugin_entry();
  return 0;
}
