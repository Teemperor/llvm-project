LD_EXTRAS := -L. -l$(LIB_PREFIX)Hub -l$(LIB_PREFIX)Leaf1 -l$(LIB_PREFIX)Leaf2 -l$(LIB_PREFIX)Leaf3 -l$(LIB_PREFIX)Leaf4 -l$(LIB_PREFIX)Leaf5
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_Leaf1 lib_Leaf2 lib_Leaf3 lib_Leaf4 lib_Leaf5 lib_Hub

lib_%:
	"$(MAKE)" -I $(SRCDIR) -f $(SRCDIR)/$*.mk DSYMUTIL=$(DSYMUTIL)

lib_Hub: lib_Leaf1 lib_Leaf2 lib_Leaf3 lib_Leaf4 lib_Leaf5
