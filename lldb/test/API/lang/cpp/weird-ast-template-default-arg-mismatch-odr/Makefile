CXX_SOURCES := main.cpp

DYLIB_CXX_SOURCES := plugin.cpp
DYLIB_NAME := plugin

# Force clang to emit the bare template name (e.g. "Grid") as the
# DW_AT_name of the DW_TAG_structure_type DIE, without the template
# arguments baked into the string. The actual arguments are only
# recoverable from the DW_TAG_template_type_parameter /
# DW_TAG_template_value_parameter child DIEs. This is what lets both
# object files name their (mutually incompatible) 'Grid<int, 4>' and
# 'Grid<int, 8>' specializations identically ("Grid") at the DWARF level,
# even though the C++ source spells the instantiation the same way
# ('Grid<int>') in both files via each one's own default template
# argument.
CFLAGS_EXTRAS := -gsimple-template-names

include Makefile.rules
