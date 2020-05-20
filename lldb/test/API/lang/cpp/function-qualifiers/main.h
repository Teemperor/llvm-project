struct C {
  int func() { return 111; }
  int func() const { return 222; }

  int const_func() const { return 333; }
  int nonconst_func() { return 444; }
};
