#ifndef DYLIB_C_H_IN
#define DYLIB_C_H_IN

// 'val' is a 'char[8]' in this dylib.
struct Triangle {
  int x;
  char val[8];
};

extern "C" {
Triangle *make_triangle_c(void);
}

#endif // DYLIB_C_H_IN
