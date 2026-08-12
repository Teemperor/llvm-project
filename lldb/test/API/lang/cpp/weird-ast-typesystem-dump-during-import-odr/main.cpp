// This test builds three translation units that each define a struct
// named 'Shared' with a different number of fields, all sharing the same
// common prefix of field names:
//
//   main.cpp (this file): struct Shared { int a; };
//   DylibB.cpp:           struct Shared { int a; int b; };
//   DylibC.cpp:           struct Shared { int a; int b; int c; };
//
// This is a three-way ODR violation: the same tag name 'Shared' denotes
// three incompatible types across the three translation units. The test
// sets a breakpoint in each of the three functions below (func_a,
// dylib_b_entry, dylib_c_entry) and, upon hitting each breakpoint in
// turn, evaluates an expression that forces LLDB's ASTImporter to import
// that translation unit's version of 'Shared' into the target's shared
// per-target scratch ASTContext, immediately followed by a
// 'target dump typesystem' command -- before continuing to the next
// breakpoint. This means the scratch ASTContext gets dumped three
// separate times, once right after each of the three conflicting
// 'Shared' definitions gets merged in.
struct Shared {
  int a;
};

extern "C" {
void func_a(Shared *s) { s->a = 1; }
void dylib_b_entry(void *p);
void dylib_c_entry(void *p);
}

int main() {
  Shared s;
  func_a(&s);
  dylib_b_entry(&s);
  dylib_c_entry(&s);
  return 0;
}
