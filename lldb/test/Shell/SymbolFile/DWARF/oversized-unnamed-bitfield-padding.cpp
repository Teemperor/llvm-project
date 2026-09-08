// Consecutive unnamed bitfields are not emitted into DWARF, so LLDB
// reconstructs them from the gap between the previous member's end and the
// following bitfield. Several unnamed bitfields in a row collapse into a single
// hole that can be wider than any integer storage unit; describing it as one
// bitfield produces a field whose value cannot be extracted (and that clang
// cannot lay out), so the hole is split into word-sized pieces instead.
//
// XFAIL: target-windows
// RUN: %clangxx_host -gdwarf -o %t %s
// RUN: %lldb %t \
// RUN:   -o "image lookup -t Many" \
// RUN:   -o "b stop" -o run \
// RUN:   -o "frame variable g_many" \
// RUN:   -o "expr g_many" \
// RUN:   -o "expr g_many.x" \
// RUN:   -o exit | FileCheck %s

struct Many {
  unsigned : 30;
  unsigned : 30;
  unsigned : 30;
  unsigned x : 2;
  unsigned y : 3;
} g_many;

// CHECK:      (lldb) image lookup -t Many
// CHECK:      struct Many {
// CHECK-NEXT:     int : 32;
// CHECK-NEXT:     int : 32;
// CHECK-NEXT:     int : 30;
// CHECK-NEXT:     unsigned int x : 2;
// CHECK-NEXT:     unsigned int y : 3;
// CHECK-NEXT: }

// CHECK:      (lldb) frame variable g_many
// CHECK-NEXT: (Many) {{.*}}g_many = (0, 0, 0, x = 3, y = 5)
// CHECK:      (lldb) expr g_many
// CHECK-NEXT: (Many) ${{[0-9]+}} = (0, 0, 0, x = 3, y = 5)
// CHECK:      (lldb) expr g_many.x
// CHECK-NEXT: (unsigned int) ${{[0-9]+}} = 3

void stop() {}

int main() {
  g_many.x = 3;
  g_many.y = 5;
  stop();
  return 0;
}
