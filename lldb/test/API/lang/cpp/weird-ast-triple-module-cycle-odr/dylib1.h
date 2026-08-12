#ifndef DYLIB1_H_IN
#define DYLIB1_H_IN

struct Cycle {
  int tag;
};

extern "C" {
void dylib1_init(void);
}

#endif // DYLIB1_H_IN
