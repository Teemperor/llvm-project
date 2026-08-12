#include "DylibX.h"

static Status gStatusX = Status::FAIL;

extern "C" {
void dylibX_init(void) {}
}

Status getX(void) { return gStatusX; }

Status (*gGetX)(void) = getX;
