DYLIB_NAME := foo
DYLIB_CXX_SOURCES := foo_top.cpp
CXX_SOURCES := main.cpp
USE_LIBDL := 1

a.out: hidden_libfoo

include Makefile.rules

# Build a second, differently-sourced copy of "libfoo.dylib" into a
# "hidden/" subdirectory, but force it to use the exact same LC_ID_DYLIB
# install name as the top-level libfoo.dylib built above (see
# DYLIB_INSTALL_NAME below). main.cpp then dlopen()s this copy via an
# explicit full path at runtime, which is what gets two same-named,
# ODR-conflicting dylibs loaded into the same process at once.
#
# This deliberately does NOT use a "lib_%:"-style pattern rule target
# name here: on macOS, the bundled GNU Make 3.81 silently treats
# ".PHONY: <pattern-rule-matched-name>" targets as already-satisfied
# no-ops and never runs the recipe if the target name is also listed in
# a non-empty .PHONY line, so this test avoids ".PHONY: hidden_libfoo"
# entirely and gives the target its own explicit (non-pattern) name.
hidden_libfoo: hidden
	"$(MAKE)" VPATH=$(SRCDIR)/hidden -C hidden -f $(MAKEFILE_RULES) \
		DYLIB_ONLY=YES DYLIB_CXX_SOURCES=foo_hidden.cpp DYLIB_NAME=foo \
		DYLIB_INSTALL_NAME=@executable_path/libfoo.dylib
