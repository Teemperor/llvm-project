#include "plugin.h"

// 'Handle' has user-provided (non-trivial) move/copy special members so
// that 'Resource' below (which wraps a 'Handle') has non-trivial, real,
// out-of-line move/copy constructors too, instead of being optimized down
// to inlined trivial pointer copies. If the move/copy constructors were
// trivial, LLDB's expression evaluator would never need to call a real
// function for them at all, and we wouldn't be able to observe any
// Sema-level difference between "callable" and "should be deleted"
// special members via expression evaluation.
struct Handle {
  int *p;
  Handle(int *p) : p(p) {}
  Handle(Handle &&other) : p(other.p) { other.p = nullptr; }
  Handle(const Handle &other) : p(other.p) {}
  ~Handle() {}
};

// This is the exe's definition of 'Resource': the move constructor is
// explicitly defaulted (usable). Declaring a move constructor suppresses
// the implicitly-declared copy constructor, so 'Resource' here has NO
// copy constructor at all (no CXXConstructorDecl for one appears in this
// module's debug info).
//
// This is a deliberate ODR violation against the dylib's same-named
// 'Resource' (see plugin.cpp), which does the exact opposite: it
// explicitly defaults its copy constructor, which suppresses ITS
// implicitly-declared move constructor.
//
// The intent is to see whether LLDB's type-system machinery (ASTImporter /
// TypeSystemClang / DWARFASTParserClang) ends up with a merged
// CXXRecordDecl for 'Resource' in the per-target shared scratch AST
// context that has BOTH a move constructor AND a copy constructor
// (Frankensteined together from the two conflicting modules), even though
// neither original definition actually has both -- and whether that
// merged decl can then be coerced into calling an operation that should
// be illegal for one of the two original definitions (e.g. moving the
// dylib's 'Resource', whose move constructor is really
// implicitly-deleted).
struct Resource {
  Handle h;
  Resource(int *p) : h(p) {}
  Resource(Resource &&) = default;
};

// Globals so debug-info for this definition of 'Resource' (including its
// defaulted move constructor) is emitted with a full definition in the
// exe's compile unit. 'r1_moved' actually odr-uses the move constructor so
// a real, callable, out-of-line function body is emitted for it (a
// defaulted special member that is never used anywhere is either omitted
// entirely or folded down to a trivial inlined copy).
Resource r1(nullptr);
Resource r1_moved((Resource &&)r1);

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
