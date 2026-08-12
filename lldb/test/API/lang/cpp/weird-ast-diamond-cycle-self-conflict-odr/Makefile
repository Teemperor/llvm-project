DYLIB_NAME := plugin
DYLIB_CXX_SOURCES := plugin.cpp
LD_EXTRAS := -L. -l$(LIB_PREFIX)left -l$(LIB_PREFIX)right -l$(LIB_PREFIX)base

CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
$(DYLIB_FILENAME): lib_base lib_left lib_right

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
