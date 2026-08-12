#include "plugin.h"

// Same template name and template argument ('double') as in main.cpp, but
// deliberately not shared via a common header, with two ODR-violating
// twists relative to main.cpp's 'Stat<double>':
//
//   1. The member order is swapped ('count' before 'sum'), so the two
//      modules' notions of 'Stat<double>' have incompatible layouts for
//      the exact same template-id.
//   2. This module never explicitly instantiates 'Stat<double>' - it is
//      only ever implicitly instantiated below, from the primary template,
//      the first time 'plugin_stat' is used. So this
//      ClassTemplateSpecializationDecl has TemplateSpecializationKind
//      TSK_ImplicitInstantiation, unlike the main executable's
//      TSK_ExplicitInstantiationDefinition for the identical template-id.
template <typename T> struct Stat {
  T count;
  T sum;
};

// Implicitly instantiates 'Stat<double>' from the primary template above.
Stat<double> plugin_stat = {3.0, 4.0};

extern "C" {
void plugin_init(void) {}

void plugin_entry() {}
}
