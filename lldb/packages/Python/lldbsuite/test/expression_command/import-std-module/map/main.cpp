#include <map>

struct C {
  C() = default;
  C(int i) : i(i) {}
  int i = 0;
  bool operator<(const C& other) const { return i < other.i; }
};

int main(int argc, char **argv) {
  C emit_default_constructor;
  std::map<C, C> m;
  m[C(1)] = C(-1);
  m[C(2)] = C(-2);
  return 0 + emit_default_constructor.i; // Set break point at this line.
}
