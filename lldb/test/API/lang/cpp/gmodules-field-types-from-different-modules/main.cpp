#include "mod1.h"
#include "mod2.h"

struct Mix {
  Use1 u1;
  Use2 u2;
};

// A record that has FieldDecls with types from two different modules.
Mix mix_modules;

int main(int argc, const char *argv[]) { return mix_modules.u1.c.i; }
