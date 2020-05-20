struct BaseWithVirtDtor {
  virtual ~BaseWithVirtDtor() {}
  virtual int foo() { return 1; }
};

struct BaseWithoutVirtDtor {
  virtual int foo() { return 2; }
};

struct DerivedWithVirtDtor : BaseWithVirtDtor {
  virtual ~DerivedWithVirtDtor() {}
  virtual int foo() { return 3; }
};

struct DerivedWithoutVirtDtor : BaseWithoutVirtDtor {
  virtual int foo() { return 4; }
};

struct DerivedWithBaseVirtDtor : BaseWithVirtDtor {
  virtual int foo() { return 5; }
};

struct DerivedWithVirtDtorButNoBaseDtor : BaseWithoutVirtDtor {
  virtual ~DerivedWithVirtDtorButNoBaseDtor() {}
  virtual int foo() { return 6; }
};

struct DerivedWithOverload : BaseWithVirtDtor {
  virtual ~DerivedWithOverload() {}
  virtual int foo(int i) { return 7; }
};

struct DerivedWithOverloadAndUsing : BaseWithVirtDtor {
  virtual ~DerivedWithOverloadAndUsing() {}
  using BaseWithVirtDtor::foo;
  virtual int foo(int i) { return 8; }
};
