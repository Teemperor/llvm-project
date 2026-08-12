LD_EXTRAS := -L. -lDylibOne -lDylibTwo
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_DylibOne lib_DylibTwo

lib_%:
	"$(MAKE)" VPATH=$(SRCDIR) -I $(SRCDIR) -f $(SRCDIR)/$*.mk
