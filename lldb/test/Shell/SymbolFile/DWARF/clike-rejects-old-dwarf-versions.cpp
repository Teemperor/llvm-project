// TypeSystemClike reads DWARF 4 and later only. The older versions spell things
// differently rather than less completely (the DWARF 2 bitfield bit-offset
// triple, DW_AT_bit_size on non-bitfields, different DW_AT_high_pc semantics),
// so reading such a unit as if it were DWARF 4 would quietly produce types of
// the wrong size and shape. Check that those units are refused outright, with a
// warning saying why, and that DWARF 4 is unaffected.
//
// UNSUPPORTED: system-windows

// RUN: %clangxx_host -gdwarf-2 -O0 -o %t-v2 %s
// RUN: %clangxx_host -gdwarf-3 -O0 -o %t-v3 %s
// RUN: %clangxx_host -gdwarf-4 -O0 -o %t-v4 %s

// A rejected unit contributes no types, and says so once.
// RUN: %lldb %t-v2 -o "b stop" -o run -o "frame variable g_pt" -o exit 2>&1 \
// RUN:   | FileCheck --check-prefix=REJECT --implicit-check-not="x = 7" %s
// RUN: %lldb %t-v3 -o "b stop" -o run -o "frame variable g_pt" -o exit 2>&1 \
// RUN:   | FileCheck --check-prefix=REJECT --implicit-check-not="x = 7" %s

// REJECT: DWARF version {{[23]}} is not supported
// REJECT-SAME: types from this module will be missing

// DWARF 4 is read as before.
// RUN: %lldb %t-v4 -o "b stop" -o run -o "frame variable g_pt" -o exit 2>&1 \
// RUN:   | FileCheck --check-prefix=ACCEPT --implicit-check-not="is not supported" %s

// ACCEPT: (Point) {{:*}}g_pt = {
// ACCEPT-NEXT: x = 7
// ACCEPT-NEXT: y = 1.5

// TypeSystemClang still reads the old versions, so the refusal is Clike's alone.
// RUN: %lldb %t-v2 -O "settings set symbols.enable-typesystem-clike false" \
// RUN:   -o "b stop" -o run -o "frame variable g_pt" -o exit 2>&1 \
// RUN:   | FileCheck --check-prefix=CLANG --implicit-check-not="is not supported" %s

// CLANG: (Point) {{:*}}g_pt = {
// CLANG-NEXT: x = 7

struct Point {
  int x;
  double y;
};

Point g_pt{7, 1.5};

void stop() {}

int main() {
  stop();
  return g_pt.x == 7 ? 0 : 1;
}
