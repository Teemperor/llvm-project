template<typename T>
struct Foo {
  T t;
  enum class X {
    A,
  };
  X x;
  X foo() {
    return x;
  }
  struct Bla {};
  Bla bla;
};

int main() {
  struct Banana {
    int t;
  };
  Banana x;
  return 0; // break here
}
