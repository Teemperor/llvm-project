#ifndef DYLIBZ_H_IN
#define DYLIBZ_H_IN

// Plain, *unscoped* enum with an implicit 'int' underlying type. Same
// name and enumerator names as DylibX's/DylibY's 'Status', but neither
// 'class' nor a fixed underlying type: this mixes a scoped-vs-unscoped
// mismatch on top of the underlying-type mismatch between DylibX/DylibY.
enum Status { OK = 0, FAIL = 1 };

extern "C" {
void dylibZ_init(void);
}

Status getZ(void);

extern Status (*gGetZ)(void);

#endif // DYLIBZ_H_IN
