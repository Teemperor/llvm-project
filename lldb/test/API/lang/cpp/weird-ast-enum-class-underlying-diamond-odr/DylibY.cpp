#include "DylibY.h"

static Status gStatusY = Status::OK;

extern "C" {
void dylibY_init(void) {}
}

Status getY(void) { return gStatusY; }

Status (*gGetY)(void) = getY;
