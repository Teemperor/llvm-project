#ifndef DYLIBA_H_IN
#define DYLIBA_H_IN

// Dylib A's view of 'lib::Widget': the sole inline-namespace member of
// 'lib' is named 'v1' and defines a one-field 'Widget'.
namespace lib {
inline namespace v1 {
struct Widget {
  int a;
  Widget(int a_) : a(a_) {}
};
} // namespace v1
} // namespace lib

extern "C" {
void dylibA_init(void);
lib::Widget *make_a(void);
}

#endif // DYLIBA_H_IN
