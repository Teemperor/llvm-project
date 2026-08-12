#ifndef DYLIB_TWO_H_IN
#define DYLIB_TWO_H_IN

// See DylibOne.h for the conflicting definition of 'Outer::Inner' (a
// 'class' with a friend declaration of 'Outer'). This dylib's 'Inner' is
// a plain 'struct' with no friend declaration and an extra field.
class Outer {
public:
  struct Inner {
    int secret;
    double extra;
  };

  Inner make() { return Inner{2, 3.5}; }
};

extern "C" {
void dylib_two_init(void);
}

extern Outer *b_outer;

#endif // DYLIB_TWO_H_IN
