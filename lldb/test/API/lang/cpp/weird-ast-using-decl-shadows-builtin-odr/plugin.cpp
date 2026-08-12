#include "plugin.h"

// See main.cpp: this dylib defines its own incompatible 'mynum::BigInt'
// (two 'int' fields instead of one 'long') and a matching 'operator+' that
// -- unlike the main executable's -- returns 'double' by value instead of
// 'BigInt'. Both modules also bring 'operator+' and 'BigInt' into the
// global namespace via the exact same using-declarations, so LLDB's
// ASTImporter has to reconcile, inside the *same* global scope of the
// shared scratch AST context, two using-shadowed 'operator+' overloads
// that take identical parameter types ('BigInt', 'BigInt') but return
// different, ABI-incompatible types (a struct returned via the sret
// convention vs. a scalar 'double' returned in a floating-point
// register). If the JIT ever picked the wrong callee/signature pairing
// for a synthesized call, this is exactly the kind of return-slot ABI
// mismatch that could crash IRGen/the JIT's calling-convention lowering
// instead of merely producing a wrong value.
namespace mynum {
struct BigInt {
  int v;
  int overflow;
};
double operator+(BigInt a, BigInt b) { return a.v + b.v; }
} // namespace mynum

using mynum::operator+;
using mynum::BigInt;

mynum::BigInt xb{5, 0};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}
