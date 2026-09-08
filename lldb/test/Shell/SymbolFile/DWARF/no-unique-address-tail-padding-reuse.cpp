// A `[[no_unique_address]]` member only occupies its dsize, so the member that
// follows it may start inside its tail padding. DWARF has no attribute for
// this, so the record LLDB reconstructs looks like it has two members whose
// storage overlaps -- which Clang's record lowering rejects outright
// (assertion "Bitfield access unit is not clipped" in CGRecordLayoutBuilder).
// Check that the expression evaluator infers the attribute back from the
// offsets, so such a record can be generated, laid out exactly as the compiler
// laid it out, and read.
//
// This is what libc++'s `__compressed_pair` (`unique_ptr`, every container) is
// built out of, so it is hit by any expression naming one of those types.
//
// XFAIL: target-windows
// RUN: %clangxx_host -gdwarf -std=c++20 -o %t %s
// RUN: %lldb %t \
// RUN:   -o "b stop" -o run \
// RUN:   -o "expr g_s" \
// RUN:   -o "expr g_s.del.b" \
// RUN:   -o "expr (int)sizeof(Structure)" \
// RUN:   -o "expr (int)((char *)&g_s.pad2 - (char *)&g_s)" \
// RUN:   -o exit | FileCheck %s

struct Empty {};
// Not POD (it holds a reference), so its 7 bytes of tail padding may be reused.
struct Del {
  int &r;
  bool b;
};
struct Pad {
  char p[7];
};

struct Structure {
  int *ptr;
  [[no_unique_address]] Empty pad1;
  [[no_unique_address]] Del del;
  // Sits at offset 17, i.e. inside `del`'s (sizeof 16) tail padding.
  [[no_unique_address]] Pad pad2;
};

int g_i = 3;
Structure g_s{&g_i, {}, {g_i, true}, {}};

// CHECK:      (lldb) expr g_s
// CHECK-NEXT: (Structure) ${{[0-9]+}} = {
// CHECK-NEXT:   ptr = 0x{{[0-9a-f]+}}
// CHECK-NEXT:   pad1 = {}
// CHECK-NEXT:   del = {
// CHECK-NEXT:     r = 0x{{[0-9a-f]+}}
// CHECK-NEXT:     b = true
// CHECK-NEXT:   }
// CHECK-NEXT:   pad2 = (p = "")
// CHECK-NEXT: }
// CHECK:      (lldb) expr g_s.del.b
// CHECK-NEXT: (bool) ${{[0-9]+}} = true
// CHECK:      (lldb) expr (int)sizeof(Structure)
// CHECK-NEXT: (int) ${{[0-9]+}} = 24
// The member offsets must come from the debug info, not from a fresh Clang
// layout of the (attribute-less) record, which would put `pad2` at 24.
// CHECK:      (lldb) expr (int)((char *)&g_s.pad2 - (char *)&g_s)
// CHECK-NEXT: (int) ${{[0-9]+}} = 17

void stop() {}

int main() {
  stop();
  return 0;
}
