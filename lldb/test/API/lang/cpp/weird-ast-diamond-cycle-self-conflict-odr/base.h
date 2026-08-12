#ifndef BASE_H_IN
#define BASE_H_IN

// 'Base' is defined in its own dylib and forms the root of the diamond:
// both Left and Right (and Top) refer to 'struct Base' via a pointer, but
// only Base's own dylib defines/instantiates it.
struct Base {
  int id;
};

extern "C" {
void base_init(void);
Base *base_get(void);
}

#endif // BASE_H_IN
