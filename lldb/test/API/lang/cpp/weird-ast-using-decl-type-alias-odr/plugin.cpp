#include "plugin.h"

// Note: deliberately not shared via a common header with main.cpp. Both
// main.cpp and this file define a namespace 'ns' containing a struct called
// 'Widget', and both pull it into file scope via a 'using ns::Widget;'
// declaration. The two 'ns::Widget' definitions are laid out completely
// differently (main.cpp's has a single 'int x' member; this one has a
// 'double y' and a 'float z', i.e. a different size and members). This is a
// genuine ODR violation, reachable only indirectly through the
// UsingShadowDecl created for the file-scope 'using' declaration rather
// than through a direct reference to 'ns::Widget'.
namespace ns {
struct Widget {
  double y;
  float z;
};
} // namespace ns

using ns::Widget;

Widget gB = {1.5, 2.5f};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}
