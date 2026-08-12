#ifndef DYLIB_B_H_IN
#define DYLIB_B_H_IN

// Only forward-declared here: the main executable never sees dylib B's
// field list for 'Aligned', it just receives a reference to dylib B's
// global through this accessor.
struct Aligned;

extern "C" {
void dylibB_init(void);
Aligned &dylibB_get(void);
}

#endif // DYLIB_B_H_IN
