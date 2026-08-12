#ifndef B_H_IN
#define B_H_IN

// The real definition of 'struct Wrapper' lives in dylib A (see a.h). Here
// in dylib B we only forward-declare it, so 'B_Type' can hold a Wrapper*
// without ever seeing A's real layout.
//
// Dylib B separately (see b_private_stub.cpp) also defines its own
// PRIVATE, conflicting 'struct Wrapper' with a totally different body,
// used only for local pointer arithmetic inside dylib B. That private
// definition is never visible through this header, but it still shows up
// in dylib B's DWARF as a second (incompatible) definition of the same tag
// name 'Wrapper'.
struct Wrapper;

struct B_Type {
  Wrapper *other;
  int b;
};

extern "C" {
void b_init(void *aPtr);
B_Type *b_get_type(void);
void b_touch_private_wrapper(void);
}

#endif // B_H_IN
