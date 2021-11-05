int other();

namespace {
struct ClassWithSize { char c; };
} //namespace

ClassWithSize class_with_size_1;

int main() {
  return class_with_size_1.c + other(); // break here
}
