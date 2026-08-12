#ifndef D4_H_IN
#define D4_H_IN

// D4's incompatible definition of 'struct Ring': payload is a 'void *'.
struct Ring {
  Ring *next;
  void *payload;
};

extern "C" {
Ring *d4_make(void);
void d4_set_next(Ring *self, Ring *next);
}

#endif // D4_H_IN
