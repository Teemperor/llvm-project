#include "DylibB.h"

static Triangle gTriangleB{2, 2.5f};

extern "C" {
Triangle *make_triangle_b(void) { return &gTriangleB; }
}
