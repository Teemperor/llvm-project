LD_EXTRAS := -L. -l$(LIB_PREFIX)dylib1 -l$(LIB_PREFIX)dylib2 -l$(LIB_PREFIX)dylib3
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_dylib1 lib_dylib2 lib_dylib3

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
