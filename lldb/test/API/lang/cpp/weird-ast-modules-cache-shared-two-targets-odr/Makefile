# This test needs two independent executables (exe1, exe2), both built
# with -fmodules -gmodules (via the "gmodules" test category / MAKE_GMODULES,
# see Test*.py), that each get their own, entirely unrelated and
# incompatible Clang module sharing the exact same module name
# ("CommonMod") and header name ("Common.h"). Each executable is built via
# its own sub-invocation of Makefile.rules (similar to the limit/nolimit
# pattern in ../incomplete-types/Makefile), each in its own build
# subdirectory so their *build-time* private module caches never collide
# with each other -- the whole point of this test is to only collide their
# *runtime* (LLDB-side) module cache, via
# "settings set symbols.clang-modules-cache-path", once both executables
# are loaded as separate targets into the very same SBDebugger.
#
# MAKE_GMODULES (set by the test via add_test_categories(["gmodules"]) and
# debug_info="gmodules") is a command-line make variable, so GNU Make's
# MAKEOVERRIDES mechanism automatically forwards it into the nested $(MAKE)
# invocations below without us having to repeat it explicitly.

all: exe1 exe2

include Makefile.rules

.PHONY:

exe1: main1.cpp CommonModExe1/Common.h CommonModExe1/module.modulemap
	$(call MKDIR_P,build_exe1)
	"$(MAKE)" -C $(BUILDDIR)/build_exe1 -f $(MAKEFILE_RULES) \
		VPATH=$(SRCDIR) \
		EXE=../exe1 CXX_SOURCES=../main1.cpp \
		CFLAGS_EXTRAS="-I$(SRCDIR)/CommonModExe1" \
		USE_PRIVATE_MODULE_CACHE=YES

exe2: main2.cpp CommonModExe2/Common.h CommonModExe2/module.modulemap
	$(call MKDIR_P,build_exe2)
	"$(MAKE)" -C $(BUILDDIR)/build_exe2 -f $(MAKEFILE_RULES) \
		VPATH=$(SRCDIR) \
		EXE=../exe2 CXX_SOURCES=../main2.cpp \
		CFLAGS_EXTRAS="-I$(SRCDIR)/CommonModExe2" \
		USE_PRIVATE_MODULE_CACHE=YES
