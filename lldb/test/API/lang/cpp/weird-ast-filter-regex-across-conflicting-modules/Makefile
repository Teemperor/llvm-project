LD_EXTRAS := -L. -l$(LIB_PREFIX)DylibOne -l$(LIB_PREFIX)DylibTwo -l$(LIB_PREFIX)DylibThree -l$(LIB_PREFIX)DylibFour
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_DylibOne lib_DylibTwo lib_DylibThree lib_DylibFour

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)
