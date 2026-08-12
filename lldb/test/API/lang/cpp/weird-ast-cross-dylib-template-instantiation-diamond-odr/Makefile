LD_EXTRAS := -L. -l$(LIB_PREFIX)Left -l$(LIB_PREFIX)Right
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_Left lib_Right

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
