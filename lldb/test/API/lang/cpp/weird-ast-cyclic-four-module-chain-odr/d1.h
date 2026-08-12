#ifndef D1_H_IN
#define D1_H_IN

// D1's incompatible definition of 'struct Ring': payload is an 'int'.
struct Ring {
  Ring *next;
  int payload;
};

extern "C" {
Ring *d1_make(void);
void d1_set_next(Ring *self, Ring *next);
}

#endif // D1_H_IN
