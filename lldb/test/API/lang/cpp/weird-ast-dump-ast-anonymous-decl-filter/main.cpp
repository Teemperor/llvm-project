#include "plugin.h"

// A completely anonymous, multi-level nested struct/union hierarchy at file
// scope, wrapped in an anonymous namespace. None of the nested
// struct/union types have a tag name, so DWARF emits several
// DW_TAG_structure_type/DW_TAG_union_type entries in a row with no
// DW_AT_name attribute at all (the enclosing DW_TAG_namespace also has no
// DW_AT_name). Clang mirrors this on the AST side with a chain of nameless
// CXXRecordDecls plus injected IndirectFieldDecls for the anonymous
// members, several levels deep:
//   (anonymous namespace)
//     -> (unnamed struct) Global
//          -> (anonymous union)
//               -> (unnamed struct)  [deepest, holds 'deep']
//               -> float f
//          -> int mid
//     -> int top
//
// This is meant to stress any code path that prints/filters decl names by
// name (e.g. "target modules dump ast --filter <pattern>"): a naive
// implementation might call something like GetName().GetCString() on one
// of these nameless decls and get back a null char* (a ConstString with no
// backing string returns nullptr, not ""), which would be a
// null-pointer-dereference waiting to happen if fed into a regex/strstr
// style comparison without a null check -- and it is more likely to be
// missed on an intermediate/nested helper than on the top-level dump path.
namespace {
struct {
  int top;
  union {
    struct {
      int deep;
    };
    float f;
  };
  int mid;
} Global;
} // namespace

int main() {
  Global.top = 1;
  Global.deep = 2;
  Global.mid = 3;

  plugin_init();
  plugin_entry();
  return 0;
}
