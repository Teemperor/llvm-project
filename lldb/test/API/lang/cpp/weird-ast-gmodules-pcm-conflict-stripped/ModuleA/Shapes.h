#ifndef MODULE_A_SHAPES_H_IN
#define MODULE_A_SHAPES_H_IN

// This is the "baseline" version of the Shapes module, only ever seen while
// building main.cpp (see Makefile's per-TU module search path). It bakes
// this exact definition of "struct Circle" into the executable's own PCM
// (built via -fmodules -gmodules).
struct Circle {
  double r;
};

inline Circle makeCircleA(double r) { return Circle{r}; }

#endif // MODULE_A_SHAPES_H_IN
