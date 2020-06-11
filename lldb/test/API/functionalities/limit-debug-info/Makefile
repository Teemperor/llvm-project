CFLAGS_EXTRAS := -flimit-debug-info
LD_EXTRAS := -L. -lone -ltwo
CXX_SOURCES := main.cpp

ONE_CXXFLAGS := -flimit-debug-info
ifdef STRIP_ONE
  ONE_CXXFLAGS += -g0
endif

TWO_CXXFLAGS := -flimit-debug-info
ifdef STRIP_TWO
  TWO_CXXFLAGS += -g0
endif

include Makefile.rules

a.out: libone libtwo

libone:
	$(MAKE) -f $(MAKEFILE_RULES) \
	  DYLIB_ONLY=YES DYLIB_CXX_SOURCES=one.cpp DYLIB_NAME=one \
	  CFLAGS_EXTRAS="$(ONE_CXXFLAGS)"

libtwo: libone
	$(MAKE) -f $(MAKEFILE_RULES) \
	  DYLIB_ONLY=YES DYLIB_CXX_SOURCES=two.cpp DYLIB_NAME=two \
	  CFLAGS_EXTRAS="$(TWO_CXXFLAGS)" LD_EXTRAS="-L. -lone"
