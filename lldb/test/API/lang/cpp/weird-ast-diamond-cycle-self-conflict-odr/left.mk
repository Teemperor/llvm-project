DYLIB_NAME := left
DYLIB_CXX_SOURCES := left.cpp
LD_EXTRAS := -L. -l$(LIB_PREFIX)base
DYLIB_ONLY := YES

include Makefile.rules

.PHONY:
$(DYLIB_FILENAME): lib_base

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
