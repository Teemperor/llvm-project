// Make sure we correctly handle $ in variable names.

int main() {
  int å = 2;
  return 0; //%self.expect("expr å", substrs=['(int) $0 = 2'])
}
