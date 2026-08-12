#ifndef DYLIB3_H_IN
#define DYLIB3_H_IN

struct Cycle {
  long tag;
  double extra2;
};

extern "C" {
void dylib3_init(void);
}

#endif // DYLIB3_H_IN
