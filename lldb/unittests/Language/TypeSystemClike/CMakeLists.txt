add_lldb_unittest(TypeSystemClikeTests
  TestClikeTypes.cpp

  LINK_LIBS
    lldbPluginTypeSystemClike
    LLVMTestingSupport
  )
