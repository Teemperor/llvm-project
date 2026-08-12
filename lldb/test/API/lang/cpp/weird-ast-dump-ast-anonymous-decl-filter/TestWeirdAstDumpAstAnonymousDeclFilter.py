"""
Test 'target modules dump ast --filter <pattern>' against a file-scope
global whose type is a deeply nested chain of completely anonymous
struct/union types with no DW_AT_name at all, several levels deep:

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
  }

DWARF emits a DW_TAG_namespace with no DW_AT_name, followed by several
DW_TAG_structure_type/DW_TAG_union_type entries that also have no
DW_AT_name, several levels deep. Clang mirrors this on the AST side with a
chain of nameless CXXRecordDecls plus injected IndirectFieldDecls for the
anonymous members.

This is meant to stress any code path that prints/filters decl names by
name: a naive implementation might call something like
GetName().GetCString() on one of these nameless decls and get back a null
char* (a ConstString with no backing string returns nullptr, not ""),
which would be a null-pointer-dereference waiting to happen if fed into a
regex/strstr style comparison without a null check -- and such a bug is
more likely to hide in an intermediate/nested helper than on the
top-level dump path, which is why this test uses several levels of
nested anonymous types rather than just one.

We exercise 'target modules dump ast' with:
  --filter ""   (empty string -- matches every decl's qualified name,
                 since std::string::find("") is always 0)
  --filter ".*" (a literal substring, NOT a regex here -- this command's
                 filter is a plain substring search, so this looks for a
                 literal ".*" substring, which will not be found)
  --filter "^$" (again a literal substring search for the two characters
                 '^' and '$', not a regex anchor pair)
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpAstAnonymousDeclFilterTestCase(TestBase):
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Force LLDB to parse the fully nested anonymous struct/union
        # hierarchy into the scratch AST context.
        self.expect_expr("Global.top", result_type="int", result_value="1")
        self.expect_expr("Global.deep", result_type="int", result_value="2")
        self.expect_expr("Global.mid", result_type="int", result_value="3")

        # An empty filter should dump every decl and, in particular,
        # must not crash while walking/printing the chain of nameless
        # struct/union decls.
        self.expect(
            "target modules dump ast --filter ''",
            substrs=[
                "NamespaceDecl",
                "CXXRecordDecl",
                "top",
                "deep",
                "mid",
            ],
        )

        # This filter is a literal substring search (not a regex), so
        # ".*" is simply a two-character string that won't match any
        # qualified decl name here. This should print nothing and, more
        # importantly, must not crash while computing/comparing the
        # (possibly empty) qualified name of an anonymous decl.
        self.expect(
            "target modules dump ast --filter '.*'",
            matching=False,
            substrs=["CXXRecordDecl"],
        )

        # Likewise "^$" is a literal two-character substring, not a
        # regex anchor pair matching only the empty string.
        self.expect(
            "target modules dump ast --filter '^$'",
            matching=False,
            substrs=["CXXRecordDecl"],
        )

        # Dumping the shared per-target scratch AST context after having
        # imported the nested anonymous decls into it should also survive.
        self.expect(
            "target dump typesystem",
            substrs=["NamespaceDecl", "CXXRecordDecl"],
        )
