CXX_SOURCES := main.cpp

DYLIB_CXX_SOURCES := plugin.cpp
DYLIB_NAME := plugin

# Don't generate a dSYM for the dylib: the whole point of this test is that
# the dylib's DWARF (and the only fully-consistent trace of ModuleB's
# conflicting "Circle") is gone once we are done building, not merely
# relocated into a .dSYM bundle.
MAKE_DSYM := NO

include Makefile.rules

# main.cpp and plugin.cpp each #include "Shapes.h", but they must resolve to
# two entirely unrelated headers (and hence two independently-hashed Clang
# modules, both named "Shapes"): main.cpp only ever sees ModuleA's "struct
# Circle" (just a radius), plugin.cpp only ever sees ModuleB's incompatible
# "struct Circle" (radius plus an extra 'area' field). Giving each
# translation unit its own, non-overlapping module search path is what
# makes the two PCMs end up with independent (and conflicting) AST nodes
# for the same module and type name.
main.o : CXXFLAGS += -I$(SRCDIR)/ModuleA
plugin.o : CXXFLAGS += -I$(SRCDIR)/ModuleB

# Strip the dylib's DWARF (but keep its symbol table) after linking. This
# removes the only place that ever fully described plugin.cpp's "Circle"
# to LLDB, leaving only a dangling reference to ModuleB's conflicting .pcm
# behind (still present in the dylib's __apple_names/module-map metadata,
# but with no debug info left that actually imports/uses it), while
# leaving the exported gPluginCircle symbol intact so the main executable
# can still resolve/relocate against it.
all: strip-plugin

strip-plugin: $(DYLIB_FILENAME)
	strip -S $(DYLIB_FILENAME)
