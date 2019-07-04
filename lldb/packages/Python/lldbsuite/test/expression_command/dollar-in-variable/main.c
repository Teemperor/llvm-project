// Make sure we correctly handle $ in variable names.

int main() {
  int foo = 24;
  int R0 = 24;
  int $foo = 42;
  int $R0 = 123; //%self.expect("expr $foo", substrs=['(int) $0 = 42'])
  return 0; //%self.expect("expr $R0", substrs=['(int) $1 = 123'])
}
