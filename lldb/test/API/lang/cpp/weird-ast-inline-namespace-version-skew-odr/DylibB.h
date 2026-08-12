#ifndef DYLIBB_H_IN
#define DYLIBB_H_IN

// Dylib B's view of 'lib::Widget': it *also* claims to be the sole
// inline-namespace member of 'lib', but names it 'v2' (a different
// version name than dylib A's 'v1') and gives 'Widget' a different,
// incompatible layout (an extra 'double b' field). This is invalid at
// the C++ language level -- a namespace can only have one inline child
// with one canonical set of members reachable via 'lib::' -- but it is
// perfectly achievable at the DWARF/debug-info level, since each dylib's
// compile unit only ever sees its own definition.
namespace lib {
inline namespace v2 {
struct Widget {
  int a;
  double b;
  Widget(int a_, double b_) : a(a_), b(b_) {}
};
} // namespace v2
} // namespace lib

extern "C" {
void dylibB_init(void);
lib::Widget *make_b(void);
}

#endif // DYLIBB_H_IN
