// Forward declare functions in tu0/tu1.
int setup_tu0();
int setup_tu1();

namespace Foo {
static const char *VarAlsoInMain = "also-main.cpp";
static const char *VarOnlyInMain = "only-main.cpp";
inline const char *OverloadedInlineFunction(int i, int j) { return "main.cpp"; }
} // namespace Foo

int main() {
  const char *x = Foo::VarAlsoInMain;
  x = Foo::VarOnlyInMain;
  x = Foo::OverloadedInlineFunction(1, 2);
  x = "foo"; // break before shared
  setup_tu0();
  setup_tu1();
  return 0; // break in main
}
