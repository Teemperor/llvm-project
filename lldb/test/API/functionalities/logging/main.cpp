struct Next {
  int x;
};

struct Foo {
  int m;
  int foo() { return 33; }
  Next n;
};
int main() {
  Foo f;
  f.foo();
  return 0; // break here
}
