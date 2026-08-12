#include "dylib_two.h"

// Same idea as dylib_one.cpp: an "Internal" struct hidden inside this
// translation unit's own unnamed namespace. Because the type lives in an
// anonymous namespace it has internal linkage, so in a standards-conforming
// world this "Internal" is a completely distinct type from the "Internal"
// defined (also inside an anonymous namespace) in dylib_one.cpp - even
// though both are spelled identically and even though LLDB will end up
// with debug info for both in play at the same time once both dylibs are
// loaded into the same process.
//
// This is the large version: three members instead of one, so the type is
// a different size than dylib_one.cpp's "Internal". If LLDB's module-scoped
// type uniquing / ASTImporter merging logic incorrectly treats same-spelled
// anonymous-namespace types from two different lldb::Modules as if they
// were one external-linkage ODR-violating type (the way the existing
// odr-handling-with-dylib test exercises for a single anonymous namespace
// within one binary), it may try to unify these two unrelated "Internal"
// RecordDecls in the scratch AST context, which can lead to a mismatched
// layout/size being used for one of the globals below and downstream
// crashes when the expression evaluator lays out or copies the value.
namespace {
struct Internal {
  int a;
  int b;
  int c;
};
} // namespace

Internal g_two = {222, 333, 444};

extern "C" {
void dylib_two_init() { g_two.a = 222; }
}
