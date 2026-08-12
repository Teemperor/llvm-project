#ifndef DYLIBB_H_IN
#define DYLIBB_H_IN

// Dylib B's own, complete definitions: a struct 'RealB' that is a totally
// different shape (and size) from dylib A's 'RealA' (see DylibA.h), but
// aliased via the *same* typedef name 'Alias'.
struct RealB {
  double y;
  int z;
};
typedef RealB Alias;

// Cross-wiring, mirroring dylib A's 'AliasB'/'RealB' pair: a typedef
// 'AliasA' naming dylib A's conflicting struct via a forward (non-
// defining) declaration only. 'RealA' is never defined here, so the
// RecordDecl for it inside dylib B's module is incomplete.
struct RealA;
typedef RealA AliasA;

extern "C" {
Alias *getB(void);
AliasA *getAliasAFromB(void);
void b_init(void);
}

#endif // DYLIBB_H_IN
