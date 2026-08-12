// Dylib A's own PRIVATE, conflicting full definition of 'B_Type'. This
// translation unit does NOT include a.h/b.h at all: it independently
// declares a struct tag named 'B_Type' with a completely different layout
// than the real 'struct B_Type' defined in dylib B (see b.cpp). Once both
// dylibs' debug info is loaded into the same LLDB target, this is a
// genuine ODR violation: two DWARF compile units each contain a DIE named
// 'B_Type', but with incompatible bodies.
struct B_Type {
  char mismatch[99];
};

// Force emission of a DWARF DIE for this private, conflicting B_Type by
// giving it an externally-visible global variable.
B_Type gPrivateBTypeInA;

extern "C" void a_touch_private_b_type(void) {
  __builtin_memset(gPrivateBTypeInA.mismatch, 0,
                    sizeof(gPrivateBTypeInA.mismatch));
}
