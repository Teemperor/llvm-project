set_property(DIRECTORY PROPERTY EXCLUDE_FROM_ALL ON)
add_lldb_library(lldbSymbolHelpers
  FakeTypeSystem.cpp

  LINK_LIBS
    lldbSymbol

  LINK_COMPONENTS
    Support
  )
include_directories(${LLVM_MAIN_SRC_DIR}/utils/unittest/googletest/include)
