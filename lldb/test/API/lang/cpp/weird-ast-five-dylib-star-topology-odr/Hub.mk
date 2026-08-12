LD_EXTRAS := -L. -l$(LIB_PREFIX)Leaf1 -l$(LIB_PREFIX)Leaf2 -l$(LIB_PREFIX)Leaf3 -l$(LIB_PREFIX)Leaf4 -l$(LIB_PREFIX)Leaf5
DYLIB_NAME := Hub
DYLIB_CXX_SOURCES := Hub.cpp
DYLIB_ONLY := YES

include Makefile.rules
