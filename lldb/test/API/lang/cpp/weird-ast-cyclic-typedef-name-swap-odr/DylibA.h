#ifndef DYLIBA_H_IN
#define DYLIBA_H_IN

// Dylib A's own, complete definitions: a struct 'RealA' aliased via the
// typedef name 'Alias'. Dylib B (see DylibB.h) defines a completely
// unrelated, differently-shaped struct 'RealB' and uses the *same*
// typedef name 'Alias' for it.
struct RealA {
  int x;
};
typedef RealA Alias;

// Cross-wiring: a second typedef, 'AliasB', that names dylib B's
// conflicting struct - but only via a forward (non-defining) declaration.
// 'RealB' is never defined in this translation unit, so the RecordDecl
// DWARFASTParserClang creates for it inside dylib A's module is
// incomplete. Dylib B mirrors this with its own 'AliasA'/'RealA' pair
// (see DylibB.h), so each dylib's debug info has a typedef pointing at an
// incomplete forward declaration of the *other* dylib's conflicting type.
struct RealB;
typedef RealB AliasB;

extern "C" {
Alias *getA(void);
AliasB *getAliasBFromA(void);
void a_init(void);
}

#endif // DYLIBA_H_IN
