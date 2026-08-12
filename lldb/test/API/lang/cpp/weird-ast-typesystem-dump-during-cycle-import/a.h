#ifndef A_H_IN
#define A_H_IN

// The real definition of 'struct B_Type' lives in dylib B (see b.h). Here
// in dylib A we only forward-declare it, so 'Wrapper' can hold a B_Type*
// without ever seeing B's real layout.
//
// Dylib A separately (see a_private_stub.cpp) also defines its own
// PRIVATE, conflicting 'struct B_Type' with a totally different body, used
// only for local pointer arithmetic inside dylib A. That private
// definition is never visible through this header, but it still shows up
// in dylib A's DWARF as a second (incompatible) definition of the same tag
// name 'B_Type'.
struct B_Type;

struct Wrapper {
  B_Type *other;
  int a;
};

extern "C" {
void a_init(void *bPtr);
Wrapper *a_get_wrapper(void);
void a_touch_private_b_type(void);
}

#endif // A_H_IN
