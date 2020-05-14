CXX_SOURCES := main.cpp
USE_LIBDL := 1

ifneq "$(BUILD_SINGLE_EXE)" ""
# The test files all compiled into one executable.
CXX_SOURCES += tu0.cpp tu1.cpp
else
# The test files but tu0 and tu1 are their own shared library.
LD_EXTRAS := -L. -llib_tu0 -llib_tu1
a.out: lib_tu0 lib_tu1

lib_tu0:
	$(MAKE) -f $(MAKEFILE_RULES) \
	DYLIB_ONLY=YES DYLIB_CXX_SOURCES=tu0.cpp DYLIB_NAME=lib_tu0

lib_tu1:
	$(MAKE) -f $(MAKEFILE_RULES) \
	DYLIB_ONLY=YES DYLIB_CXX_SOURCES=tu1.cpp DYLIB_NAME=lib_tu1
endif

include Makefile.rules
