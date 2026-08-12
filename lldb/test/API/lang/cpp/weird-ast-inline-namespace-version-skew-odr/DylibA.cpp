#include "DylibA.h"

lib::Widget *gWidgetA = nullptr;

extern "C" {
void dylibA_init() { gWidgetA = new lib::Widget(1); }
lib::Widget *make_a() { return new lib::Widget(11); }
}
