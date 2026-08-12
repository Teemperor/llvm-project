#ifndef PLUGIN_H_IN
#define PLUGIN_H_IN

extern "C" {
void plugin_init(void);
void plugin_entry(void);

// Implemented in the dylib where 'Opaque' is a complete, polymorphic type.
// The main executable only ever sees 'class Opaque;' (forward declaration)
// and stores whatever this returns into a global 'Opaque *' without ever
// completing the type itself.
void *make_opaque(void);
}

#endif // PLUGIN_H_IN
