#ifndef DYLIB_A_H_IN
#define DYLIB_A_H_IN

// 'val' is an 'int' in this dylib.
struct Triangle {
  int x;
  int val;
};

extern "C" {
Triangle *make_triangle_a(void);
}

#endif // DYLIB_A_H_IN
