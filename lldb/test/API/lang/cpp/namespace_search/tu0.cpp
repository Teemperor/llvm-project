// Same contents as tu1.cpp. Used for testing in which order LLDB searches
// modules/TUs during lookup.
// Note: This file should always come before tu1.cpp (both alphabetically
// and in compiler/linker invocations) so that all checks that evaluate
// expressions in tu1.cpp know that LLDB found declarations in tu1 not just
// by picking the first declaration it found when iterating over some list
// of modules/TUs.

namespace Foo {
static const char *VarAlsoInMain = "tu0.cpp";
static const char *VarNotInMain = "tu0.cpp";
inline const char *OverloadedInlineFunction(int i) { return "tu0.cpp"; }
} // namespace Foo

const char *setup_tu0() {
  const char *x = Foo::OverloadedInlineFunction(1);
  x = Foo::VarAlsoInMain;
  x = Foo::VarNotInMain;
  return x;
}
