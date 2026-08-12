#ifndef MODULE_B_SHAPES_H_IN
#define MODULE_B_SHAPES_H_IN

// This is an incompatible, differently-shaped version of the very same
// "Shapes" module: it is only ever seen while building plugin.cpp (see
// Makefile's per-TU module search path), and it declares "struct Circle"
// with an extra 'area' field. Because the module map/header content
// differs from ModuleA/Shapes.h, Clang gives this its own, independently
// hashed .pcm in the shared module cache -- there is no single consistent
// definition of "Circle" anywhere in the (notional) program.
struct Circle {
  double r;
  double area;
};

inline Circle makeCircleB(double r, double area) {
  return Circle{r, area};
}

#endif // MODULE_B_SHAPES_H_IN
