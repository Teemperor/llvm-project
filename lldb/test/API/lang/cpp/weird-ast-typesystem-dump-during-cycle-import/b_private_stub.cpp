// Dylib B's own PRIVATE, conflicting full definition of 'Wrapper'. This
// translation unit does NOT include a.h/b.h at all: it independently
// declares a struct tag named 'Wrapper' with a completely different
// layout than the real 'struct Wrapper' defined in dylib A (see a.cpp).
// Once both dylibs' debug info is loaded into the same LLDB target, this
// is a genuine ODR violation: two DWARF compile units each contain a DIE
// named 'Wrapper', but with incompatible bodies.
struct Wrapper {
  char mismatch[77];
};

// Force emission of a DWARF DIE for this private, conflicting Wrapper by
// giving it an externally-visible global variable.
Wrapper gPrivateWrapperInB;

extern "C" void b_touch_private_wrapper(void) {
  __builtin_memset(gPrivateWrapperInB.mismatch, 0,
                    sizeof(gPrivateWrapperInB.mismatch));
}
