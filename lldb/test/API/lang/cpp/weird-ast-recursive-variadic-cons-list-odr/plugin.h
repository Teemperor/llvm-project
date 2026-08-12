#ifndef PLUGIN_H_IN
#define PLUGIN_H_IN

extern "C" {
// 'main_list_ptr' is a pointer to main.cpp's 'main_list' (typed as
// 'void *' here since main.cpp and plugin.cpp each see a different,
// conflicting definition of the base-case specialization 'Cons<int>' that
// the recursive specializations 'Cons<int,int>' and 'Cons<int,int,int>'
// bottom out into). Used by plugin_init() to wire the dylib's innermost
// (conflicting-layout) 'Cons<int>' node's 'rest' pointer across the module
// boundary onto the main executable's top-level list, so that a single
// expression evaluated while stopped in the dylib has to walk from the
// dylib's list back into the main executable's list. Passed in explicitly
// (rather than looked up via a symbol defined in the main executable)
// since the dylib is built and linked standalone and cannot resolve
// symbols from the executable at link time.
void plugin_init(void *main_list_ptr);
void plugin_entry(void);
}

#endif // PLUGIN_H_IN
