LD_EXTRAS := -L. -lDylibA -lDylibB
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_DylibA lib_DylibB

lib_%:
	"$(MAKE)" VPATH=$(SRCDIR) -I $(SRCDIR) -f $(SRCDIR)/$*.mk
