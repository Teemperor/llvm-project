CXX_SOURCES := main.cpp

DYLIB_CXX_SOURCES := plugin.cpp
DYLIB_NAME := plugin

include Makefile.rules

# main.cpp and plugin.cpp each #include "Shared.h", but they must resolve to
# two entirely unrelated headers (and hence two unrelated Clang modules):
# main.cpp only ever sees ModuleA's "struct Shared", plugin.cpp only ever
# sees ModuleB's "struct Shared". Giving each translation unit its own,
# non-overlapping module search path is what makes the two PCMs end up with
# independent (and conflicting) AST nodes for the same type name.
main.o : CXXFLAGS += -I$(SRCDIR)/ModuleA
plugin.o : CXXFLAGS += -I$(SRCDIR)/ModuleB
