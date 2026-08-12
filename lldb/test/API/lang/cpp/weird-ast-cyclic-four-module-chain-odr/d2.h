#ifndef D2_H_IN
#define D2_H_IN

// D2's incompatible definition of 'struct Ring': payload is a 'double'.
struct Ring {
  Ring *next;
  double payload;
};

extern "C" {
Ring *d2_make(void);
void d2_set_next(Ring *self, Ring *next);
}

#endif // D2_H_IN
