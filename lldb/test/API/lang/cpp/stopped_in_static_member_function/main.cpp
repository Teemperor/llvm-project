struct A {
  int member_var = 1;
  static int static_member_var;
  int member_func() { return 3; }
  static int static_func() { return 4; }

  static int context_static_func() {
    int i = static_member_var;
    i += static_func();
    return i; // break in static member function
  }

  int context_member_func() {
    int i = member_var;
    i += member_func();
    return i; // break in member function
  }
};

int A::static_member_var = 2;

int main() {
  int i = A::context_static_func();
  A a;
  a.context_member_func();
  return i;
}
