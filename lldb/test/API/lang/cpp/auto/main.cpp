auto func();
auto func() {
  return 1L;
}

struct C {
  int member = 3;
  auto func();
};

auto C::func() {
  return 2L;
}

int main() {
  long foo = 1234;
  C c;
  return c.func() + func(); // break here
}
