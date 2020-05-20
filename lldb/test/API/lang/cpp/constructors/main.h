struct ClassWithImplicitCtor {
  int foo() { return 1; }
};

struct ClassWithDefaultedCtor {
  ClassWithDefaultedCtor() = default;
  int foo() { return 2; }
};

struct ClassWithOneCtor {
  int value;
  ClassWithOneCtor(int i) { value = i; }
};

struct ClassWithMultipleCtor {
  int value;
  ClassWithMultipleCtor(int i) { value = i; }
  ClassWithMultipleCtor(int i, int v) { value = v + i; }
};

struct ClassWithDeletedCtor {
  int value;
  ClassWithDeletedCtor() { value = 6; }
  ClassWithDeletedCtor(int i) = delete;
};

struct ClassWithDeletedDefaultCtor {
  int value;
  ClassWithDeletedDefaultCtor() = delete;
  ClassWithDeletedDefaultCtor(int i) { value = i; }
};
