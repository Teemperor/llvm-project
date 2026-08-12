CXX_SOURCES := main.cpp

DYLIB_CXX_SOURCES := plugin.cpp
DYLIB_NAME := plugin

# Don't generate a dSYM for the dylib: the whole point of this test is that
# no module anywhere (main executable, dylib or dSYM) retains a complete
# definition of 'struct Handle' once we are done building.
MAKE_DSYM := NO

include Makefile.rules

# Strip the dylib's debug info (but keep its symbol table) after linking.
# This removes the only complete definition of 'struct Handle' that ever
# existed, while leaving the exported g_handle symbol intact so the main
# executable can still resolve/relocate against it.
all: strip-plugin

strip-plugin: $(DYLIB_FILENAME)
	strip -S $(DYLIB_FILENAME)
