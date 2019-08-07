class Foo {};
class Bar : public Foo {};

class Base {
public:
  virtual Foo* foo() { return nullptr; }
};

class Derived : public Base {
public:
  Bar* foo() override { return nullptr; }
};

int main() {
  Derived d;
  Base *b = &d;
  (void)b->foo(); //%self.expect("expr d.foo()", substrs=['(Bar *)', ' = nullptr'])
  return 0;
}
