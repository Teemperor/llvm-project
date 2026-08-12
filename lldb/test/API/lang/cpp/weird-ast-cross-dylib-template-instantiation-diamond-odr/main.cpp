// Note: deliberately not shared via a common header with Left.cpp/
// Right.cpp: main.cpp never sees a body (or even a declaration) for the
// class template 'Box' at all, so it cannot pick a "winning" definition
// on its own. All of the actual ODR conflict is between Left.cpp's and
// Right.cpp's differing views of 'Box' (see Left.h/Right.h): both define
// an identically-shaped template
//     template <typename T> struct Box { T val; int tag; char pad[PAD_SIZE]; };
// and instantiate 'Box<int>', but Left.h's PAD_SIZE is 1 and Right.h's
// PAD_SIZE is 4 (standing in for a non-type template default argument
// that got resolved differently on each side of a "diamond" header
// include). Both dylibs' 'Box<int>' therefore end up with the exact same
// name/mangling in the debug info, despite disagreeing about
// 'sizeof(Box<int>)' and the layout of the trailing 'pad' member.

extern "C" {
void left_init(void);
void right_init(void);
}

extern "C" void main_entry() {}

int main() {
  left_init();
  right_init();
  main_entry();
  return 0;
}
