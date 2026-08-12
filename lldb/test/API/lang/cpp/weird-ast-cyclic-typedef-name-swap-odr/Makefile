LD_EXTRAS := -L. -l$(LIB_PREFIX)DylibA -l$(LIB_PREFIX)DylibB
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_DylibA lib_DylibB

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
