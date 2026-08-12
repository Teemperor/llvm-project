#include "plugin.h"

// Small version of the C-style "typedef struct { ... } Foo;" idiom as seen
// by the main executable. The struct itself has no tag name - it is only
// ever named through the typedef "Foo". This means the RecordDecl Clang
// creates for the struct body is anonymous (unnamed), and the only path
// back to a name for it is via the TypedefDecl "Foo" that points at it.
//
// The dylib (plugin.cpp) defines its own, differently-shaped anonymous
// struct and also calls its typedef "Foo". Because both RecordDecls are
// unnamed, the ASTImporter's usual named-tag ODR/redeclaration matching
// (which keys off the tag's DeclarationName) does not apply the same way;
// instead any matching has to go through the typedef name or structural
// comparison of the anonymous RecordDecl. This test wants to exercise that
// under-tested path.
typedef struct {
  int a;
} Foo;

Foo main_foo = {1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
