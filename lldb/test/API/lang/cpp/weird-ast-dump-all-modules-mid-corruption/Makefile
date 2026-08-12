LD_EXTRAS := -L. -ldylib1 -ldylib2
CXX_SOURCES := main.cpp

include Makefile.rules

.PHONY:
a.out: lib_dylib1 lib_dylib2

lib_%:
	"$(MAKE)" VPATH=$(SRCDIR) -I $(SRCDIR) -f $(SRCDIR)/$*.mk
