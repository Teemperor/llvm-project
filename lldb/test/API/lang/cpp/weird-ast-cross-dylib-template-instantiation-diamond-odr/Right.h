#ifndef RIGHT_H_IN
#define RIGHT_H_IN

// Right's view of the *same-looking* class template 'Box'. See Left.h:
// this header defines the identical template head/body shape (a single
// type parameter T), but with PAD_SIZE resolved to 4 instead of 1 (as if
// reached through a differently-ordered set of includes than the copy
// Left saw). Clang/DWARF therefore records this specialization under the
// exact same name 'Box<int>' as Left's, despite the differing 'pad' size.
#define PAD_SIZE 4

template <typename T> struct Box {
  T val;
  int tag;
  char pad[PAD_SIZE];
};

extern "C" {
void right_init(void);
}

extern Box<int> *gRightBox;

#endif // RIGHT_H_IN
