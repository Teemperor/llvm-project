add_lldb_unittest(TypeSystemClikeUnitTests
  IdentifierTest.cpp
  LanguageOptsTest.cpp
  TestNamedType.cpp
  TypeTest.cpp

  LINK_LIBS
    lldbPluginTypeSystemClike
  )
