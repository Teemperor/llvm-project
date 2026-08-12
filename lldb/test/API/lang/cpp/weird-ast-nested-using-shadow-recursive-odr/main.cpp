#include "plugin.h"

// This is the "a.cpp" side of the scenario: two levels of chained
// using-declarations re-export the same struct name.
//
//   outer::inner::Data - the real definition.
//   outer::Data         - `using inner::Data;` inside namespace outer,
//                          re-exporting inner::Data one level up.
//   ::Data               - `using outer::Data;` at global scope,
//                          re-exporting outer::Data (which itself is a
//                          re-export of inner::Data) one more level up.
//
// Each using-declaration is its own DW_TAG_imported_declaration in the
// debug info, and when LLDB actually needs to materialize one of these as
// a clang::Decl (see DWARFASTParserClang::GetClangDeclForDIE), it creates a
// real clang::UsingDecl wrapping a clang::UsingShadowDecl whose target can
// itself be another UsingShadowDecl. So "outer::Data" here is backed by a
// UsingShadowDecl chain of depth two before it bottoms out at the actual
// CXXRecordDecl for inner::Data.
//
// The dylib (plugin.cpp) builds the exact same two-level using-declaration
// chain for its own, differently-shaped "Data" (three members instead of
// one), so that both the main executable and the dylib have independent,
// ODR-conflicting versions of "outer::inner::Data" that are each reachable
// through the same nested using-shadow chain.
namespace outer {
namespace inner {
struct Data {
  int v;
};
} // namespace inner
using inner::Data;
} // namespace outer
using outer::Data;

Data da{1};

int main() {
  plugin_init();
  return 0;
}
