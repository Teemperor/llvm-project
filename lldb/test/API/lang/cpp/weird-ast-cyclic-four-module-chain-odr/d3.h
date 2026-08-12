#ifndef D3_H_IN
#define D3_H_IN

// D3's incompatible definition of 'struct Ring': payload is an anonymous
// struct of two ints.
struct Ring {
  Ring *next;
  struct {
    int a, b;
  } payload;
};

extern "C" {
Ring *d3_make(void);
void d3_set_next(Ring *self, Ring *next);
}

#endif // D3_H_IN
