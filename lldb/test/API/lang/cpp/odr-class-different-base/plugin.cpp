#include "plugin.h"

// See main.cpp: this 'Shape' has the same name as the one in main.cpp, but
// derives from a different base class (BaseB instead of BaseA).
class BaseB {
public:
  int b = 10;
};

class Shape : public BaseB {
public:
  int shapeField = 20;
};

Shape *gPluginShape = nullptr;

extern "C" {
void plugin_init() { gPluginShape = new Shape(); }

void plugin_entry() {}
}
