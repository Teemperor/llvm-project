LD_EXTRAS := -L. -l$(LIB_PREFIX)DylibA -l$(LIB_PREFIX)DylibB -l$(LIB_PREFIX)DylibC
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_DylibA lib_DylibB lib_DylibC

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
