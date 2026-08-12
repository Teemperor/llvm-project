#ifndef DYLIBX_H_IN
#define DYLIBX_H_IN

#include <cstdint>

// Scoped enum with an explicit 1-byte underlying type.
enum class Status : uint8_t { OK = 0, FAIL = 1 };

extern "C" {
void dylibX_init(void);
}

// Real, debug-info-visible function returning DylibX's 'Status' by value.
Status getX(void);

// Global function pointer to getX(), so debugger expressions can call
// through 'gGetX' the same way they call through 'gGetY'/'gGetZ'.
extern Status (*gGetX)(void);

#endif // DYLIBX_H_IN
