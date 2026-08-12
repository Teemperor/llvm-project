#include "DylibZ.h"

static Status gStatusZ = FAIL;

extern "C" {
void dylibZ_init(void) {}
}

Status getZ(void) { return gStatusZ; }

Status (*gGetZ)(void) = getZ;
