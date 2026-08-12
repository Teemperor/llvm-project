#include "DylibC.h"

static Triangle gTriangleC{3, "hello"};

extern "C" {
Triangle *make_triangle_c(void) { return &gTriangleC; }
}
