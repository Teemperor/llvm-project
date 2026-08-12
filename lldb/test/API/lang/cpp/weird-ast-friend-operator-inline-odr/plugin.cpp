#include "plugin.h"

// See main.cpp for the full scenario description. This is the dylib side
// of the ODR violation: 'Token' has a different field layout ('long
// kind' plus an extra 'char tag', instead of main.cpp's single 'int
// kind'), and its inline friend 'operator==' has different comparison
// semantics (it also compares 'tag'). Just like main.cpp's 'Token', this
// 'operator==' is only findable via ADL -- it is never declared at
// namespace scope.
struct Token {
  long kind;
  char tag;
  friend bool operator==(const Token &a, const Token &b) {
    return a.kind == b.kind && a.tag == b.tag;
  }
};

Token tb{1, 0};

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Reference tb (and its inline friend 'operator==') so debug info for
  // it definitely gets emitted.
  bool eq = (tb == tb);
  (void)eq;
}
}
