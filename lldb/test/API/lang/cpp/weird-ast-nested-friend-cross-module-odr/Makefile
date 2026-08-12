LD_EXTRAS := -L. -l$(LIB_PREFIX)DylibOne -l$(LIB_PREFIX)DylibTwo
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_DylibOne lib_DylibTwo

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
