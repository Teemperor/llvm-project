add_lldb_unittest(TypeSystemClikeUnitTests
  BuiltinTypesTest.cpp
  IdentifierTest.cpp
  LanguageOptsTest.cpp
  TypeTest.cpp

  LINK_LIBS
    lldbPluginTypeSystemClike
  )
