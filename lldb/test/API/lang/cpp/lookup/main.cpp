namespace other {
  int func() { return 123; }
}

int global_func() { return 1; }

namespace ns {
  int ns_func() { return 2; }
  int function_to_enter() {
    return ns_func(); // break in ns
  }
  namespace nested {
    int nested_func() { return 3; }
    int nested_function_to_enter() {
      return nested_func(); // break in ns::nested
    }
  }
}

int main() {
  int i = global_func();
  ns::function_to_enter();
  ns::nested::nested_function_to_enter();
  return 0; // break in main
}
