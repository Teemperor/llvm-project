#include "plugin.h"

// See plugin.cpp: this dylib defines its own, incompatible 'ns::Widget' with
// the same unqualified name and pulls it into file scope with a 'using'
// declaration too, forming a genuine ODR violation once both this
// executable's and the dylib's 'ns::Widget' get imported into LLDB's shared
// per-target scratch AST context.
namespace ns {
struct Widget {
  int x;
};
} // namespace ns

using ns::Widget;

Widget gA = {42};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}
