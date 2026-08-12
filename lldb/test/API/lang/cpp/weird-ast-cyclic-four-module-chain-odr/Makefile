LD_EXTRAS := -L. -l$(LIB_PREFIX)d1 -l$(LIB_PREFIX)d2 -l$(LIB_PREFIX)d3 -l$(LIB_PREFIX)d4
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_d1 lib_d2 lib_d3 lib_d4

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
