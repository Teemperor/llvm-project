CXX_SOURCES := main.cpp

DYLIB_CXX_SOURCES := plugin.cpp
DYLIB_NAME := plugin

# Number of chained Tag<N> template instantiations (Tag<1> .. Tag<N_MAX>,
# terminating at the Tag<0> base case) to generate, split roughly in half
# between main.cpp (even N) and plugin.cpp (odd N, with a conflicting field
# order). See gen_tags.py for details.
N_MAX := 200

include Makefile.rules

# main.cpp/plugin.cpp each '#include' one of these generated headers before
# they can be compiled.
tag_main_gen.h:
	python3 "$(SRCDIR)/gen_tags.py" main $(N_MAX) tag_main_gen.h

tag_plugin_gen.h:
	python3 "$(SRCDIR)/gen_tags.py" plugin $(N_MAX) tag_plugin_gen.h

main.o: tag_main_gen.h
plugin.o: tag_plugin_gen.h
