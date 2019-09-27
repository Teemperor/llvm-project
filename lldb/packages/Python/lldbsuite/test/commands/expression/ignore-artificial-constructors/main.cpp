struct Foo {
  virtual ~Foo() = default;
};

int main() {
  Foo f;
  return 0; //%self.expect("expr Foo()", substrs=["(Foo) $0 = {}"])
}
