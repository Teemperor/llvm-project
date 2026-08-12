# Six tiny dylibs (M1 .. M6) each get their own generated 'commonN.h',
# produced from the shared 'common.h.in' template by substituting the
# array bound '@N@' with a distinct literal (1..6) via sed. Every one of
# the six generated headers still defines the *same* struct tag name
# ('Common', via '#define COMMON_NAME Common' in common.h.in) but with a
# different 'fields' array length -- a deliberate ODR violation on the
# array bound, replicated six ways across six separate modules instead of
# the usual two or three.
LD_EXTRAS := -L. -lM1 -lM2 -lM3 -lM4 -lM5 -lM6
CXX_SOURCES := main.cpp

# The generated commonN.h headers land in the build directory (cwd), not
# in $(SRCDIR) where main.cpp/M<N>.cpp physically live, so add '-I.'
# explicitly -- plain quoted '#include "commonN.h"' only searches the
# including file's own directory by default.
CFLAGS_EXTRAS := -I.

include Makefile.rules

.PHONY:
a.out: lib_M1 lib_M2 lib_M3 lib_M4 lib_M5 lib_M6

lib_%:
	"$(MAKE)" VPATH=$(SRCDIR) -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)

common1.h: $(SRCDIR)/common.h.in
	sed 's/@N@/1/' "$(SRCDIR)/common.h.in" > common1.h

common2.h: $(SRCDIR)/common.h.in
	sed 's/@N@/2/' "$(SRCDIR)/common.h.in" > common2.h

common3.h: $(SRCDIR)/common.h.in
	sed 's/@N@/3/' "$(SRCDIR)/common.h.in" > common3.h

common4.h: $(SRCDIR)/common.h.in
	sed 's/@N@/4/' "$(SRCDIR)/common.h.in" > common4.h

common5.h: $(SRCDIR)/common.h.in
	sed 's/@N@/5/' "$(SRCDIR)/common.h.in" > common5.h

common6.h: $(SRCDIR)/common.h.in
	sed 's/@N@/6/' "$(SRCDIR)/common.h.in" > common6.h

lib_M1: common1.h
lib_M2: common2.h
lib_M3: common3.h
lib_M4: common4.h
lib_M5: common5.h
lib_M6: common6.h
