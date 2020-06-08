struct A {
  const static int i = 3;
};

int main() {
  return A::i; // break here
}
