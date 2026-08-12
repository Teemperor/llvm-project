#ifndef LEFT_H_IN
#define LEFT_H_IN

// Left's view of the class template 'Box'. PAD_SIZE stands in for a
// non-type template default argument that got resolved differently on
// this side of a "diamond" include (e.g. because a shared header was
// reordered relative to Right's copy -- see Right.h). 'Box' itself only
// has a single (type) template parameter here, so the instantiation
// 'Box<int>' has the *exact* same name/mangling as Right's -- but with a
// different 'pad' array size, and therefore a different byte size and
// field layout.
#define PAD_SIZE 1

template <typename T> struct Box {
  T val;
  int tag;
  char pad[PAD_SIZE];
};

extern "C" {
void left_init(void);
}

extern Box<int> *gLeftBox;

#endif // LEFT_H_IN
