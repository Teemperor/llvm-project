#include "DylibA.h"

static Triangle gTriangleA{1, 100};

extern "C" {
Triangle *make_triangle_a(void) { return &gTriangleA; }
}
