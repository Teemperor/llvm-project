#include "plugin.h"

// This translation unit deliberately never spells out 'Point' itself (it
// only calls the extern "C" accessors declared elsewhere), so that which
// of the two conflicting 'Point' definitions gets pulled in is entirely
// decided by the test's expression evaluation, not by this file.
extern "C" {
void plugin_init(void) {}
void plugin_entry(void) {}
}
