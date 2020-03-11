template<typename T>
int foo(T t1) {
        return int(t1);
}

// Some cases to cover ADL
namespace A {
struct C {};

template <typename T> int f(T) { return 4; }

template <typename T> int g(T) { return 4; }
} // namespace A

int f(int) { return 1; }

template <class T> int h(T x) { return x; }

int h(double d) { return 5; }

template <class... Us> int var(Us... pargs) { return 10; }

// Having the templated overloaded operators in a namespace effects the
// mangled name generated in the IR e.g. _ZltRK1BS1_ Vs _ZN1AltERKNS_1BES2_
// One will be in the symbol table but the other won't. This results in a
// different code path that will result in CPlusPlusNameParser being used.
// This allows us to cover that code as well.
namespace A {
template <typename T> bool operator<(const T &, const T &) { return true; }

template <typename T> bool operator>(const T &, const T &) { return true; }

template <typename T> bool operator<<(const T &, const T &) { return true; }

template <typename T> bool operator>>(const T &, const T &) { return true; }

template <typename T> bool operator==(const T &, const T &) { return true; }

struct B {};
} // namespace A

struct C {};

// Make sure we cover more straight forward cases as well.
bool operator<(const C &, const C &) { return true; }
bool operator>(const C &, const C &) { return true; }
bool operator>>(const C &, const C &) { return true; }
bool operator<<(const C &, const C &) { return true; }
bool operator==(const C &, const C &) { return true; }
int operator<=>(const C &, const C &) { return 0; }

int main() {
  A::B b1;
  A::B b2;
  C c1;
  C c2;

  bool result_b = b1 < b2 && b1 << b2 && b1 == b2 && b1 > b2 && b1 >> b2;
  bool result_c = c1 < c2 && c1 << c2 && c1 == c2 && c1 > c2 && c1 >> c2;
  int x = c1<=>c2;
  return foo(42) + result_b + result_c +
         // ADL lookup case,
         f(A::C{}) +
         // ADL lookup but no overload
         g(A::C{}) +
         // overload with template
         h(10) + h(1.) +
         // variadic function
         var(1) + var(1, 2); // break here
}
