namespace Foo {
static const char *VarAlsoInMain = "tu1.cpp";
static const char *VarNotInMain = "tu1.cpp";
inline const char *OverloadedInlineFunction(int i) { return "tu1.cpp"; }
} // namespace Foo

const char *setup_tu1() {
  const char *x = Foo::OverloadedInlineFunction(1);
  x = Foo::VarAlsoInMain;
  x = Foo::VarNotInMain;
  return x; // break in other tu
}
