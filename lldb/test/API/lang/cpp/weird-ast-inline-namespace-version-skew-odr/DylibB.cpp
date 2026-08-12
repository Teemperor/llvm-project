#include "DylibB.h"

lib::Widget *gWidgetB = nullptr;

extern "C" {
void dylibB_init() { gWidgetB = new lib::Widget(2, 2.5); }
lib::Widget *make_b() { return new lib::Widget(22, 22.5); }
}
