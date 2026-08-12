#ifndef PLUGIN_H_IN
#define PLUGIN_H_IN

extern "C" {
// 'main_head_ptr' is a pointer to main.cpp's 'main_head' (typed as 'void *'
// here since main.cpp and plugin.cpp each see a different, conflicting
// definition of 'Node<int>'). Used by plugin_init() to wire the dylib's
// 'Node<int>' list onto the main executable's 'Node<int>' list, so that the
// two conflicting specializations of the same self-referential template end
// up chained together at runtime. Passed in explicitly (rather than looked
// up via a symbol defined in the main executable) since the dylib is built
// and linked standalone and cannot resolve symbols from the executable at
// link time.
void plugin_init(void *main_head_ptr);
void plugin_entry(void);
}

#endif // PLUGIN_H_IN
