LD_EXTRAS := -L. -l$(LIB_PREFIX)DylibX -l$(LIB_PREFIX)DylibY -l$(LIB_PREFIX)DylibZ
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_DylibX lib_DylibY lib_DylibZ

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
