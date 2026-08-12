#ifndef DYLIB2_H_IN
#define DYLIB2_H_IN

struct Cycle {
  int tag;
  int extra1;
};

extern "C" {
void dylib2_init(void);
}

#endif // DYLIB2_H_IN
