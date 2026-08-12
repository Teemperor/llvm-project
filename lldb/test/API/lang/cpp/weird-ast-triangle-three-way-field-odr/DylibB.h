#ifndef DYLIB_B_H_IN
#define DYLIB_B_H_IN

// 'val' is a 'float' in this dylib.
struct Triangle {
  int x;
  float val;
};

extern "C" {
Triangle *make_triangle_b(void);
}

#endif // DYLIB_B_H_IN
