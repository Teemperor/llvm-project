// Make sure we correctly handle unicode in variable names.

struct A {
  // We need a member variable in the context that could shadow our local
  // variable. If our optimization code fails to handle this, then we won't
  // correctly inject our local variable so that it won't get shadowed.
  int ößœçå = 2;
  int foo() {
    int ößœçå = 3;
    return ößœçå; //%self.expect("expr ößœçå", substrs=['(int) $0 = 3'])
  }
};

int main() {
  A a;
  return a.foo();
}
