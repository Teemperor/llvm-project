#ifndef PLUGIN_H_IN
#define PLUGIN_H_IN

extern "C" {
// Defined by the "top-level" libfoo.dylib (built from foo_top.cpp).
void *libfoo_top_init(void);
}

// Symbol name looked up (via dlsym) in the "hidden" copy of libfoo.dylib
// (built from foo_hidden.cpp) after it is dlopen()-ed from main.cpp. Its
// signature matches 'libfoo_top_init' above: it returns a 'void *' that
// actually points at the hidden dylib's own (differently-shaped) 'Widget'.
#define HIDDEN_INIT_SYMBOL "libfoo_hidden_init"

#endif // PLUGIN_H_IN
