add_lldb_unittest(TypeSystemCppTests
  TestCppTypes.cpp

  LINK_LIBS
    lldbPluginTypeSystemCpp
    LLVMTestingSupport
  )
