// The main executable deliberately never includes DylibOne.h or
// DylibTwo.h: it only calls each dylib's init function and then stops at
// a breakpoint, letting the test's expression evaluator pull in each
// dylib's conflicting 'Outer'/'Outer::Inner' definitions on demand. See
// DylibOne.h/DylibOne.cpp and DylibTwo.h/DylibTwo.cpp for the actual
// conflicting 'Outer' definitions (nested 'Inner' as a friended 'class'
// vs. a plain 'struct' with an extra field).
extern "C" {
void dylib_one_init(void);
void dylib_two_init(void);
}

void nested_friend_entry() {
  // Breakpoint here. By this point both dylibs have run their *_init
  // functions and the global pointers 'a_outer' (DylibOne) and 'b_outer'
  // (DylibTwo) are both initialized.
}

int main() {
  dylib_one_init();
  dylib_two_init();
  nested_friend_entry();
  return 0;
}
