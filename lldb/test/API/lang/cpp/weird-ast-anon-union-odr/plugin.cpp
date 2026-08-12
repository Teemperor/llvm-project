#include "plugin.h"

// Large version of Variant's anonymous union as seen by the dylib. Same
// struct name ("Variant") and same first field ("tag"), but the anonymous
// union has additional, larger members compared to the definition in
// main.cpp. This makes the two "Variant" RecordDecls disagree not just on
// size, but on the *set* of IndirectFieldDecls Clang synthesizes for the
// anonymous union (i, f, d, buf here vs. just i, f in the main executable).
struct Variant {
  int tag;
  union {
    int i;
    float f;
    double d;
    char buf[16];
  };
};

Variant plugin_variant = {1, {0}};

extern "C" {
void plugin_init() { plugin_variant.d = 3.5; }

void plugin_entry() {}
}
