#ifndef DYLIBY_H_IN
#define DYLIBY_H_IN

#include <cstdint>

// Scoped enum with an explicit 2-byte underlying type.
enum class Status : int16_t { OK = 0, FAIL = 1 };

extern "C" {
void dylibY_init(void);
}

Status getY(void);

extern Status (*gGetY)(void);

#endif // DYLIBY_H_IN
