add_lldb_unittest(TypeSystemClikeUnitTests
  BuiltinTypesTest.cpp
  IdentifierTest.cpp
  LanguageOptsTest.cpp
  TestByteSizedType.cpp
  TestNamedType.cpp
  TypeTest.cpp

  LINK_LIBS
    lldbPluginTypeSystemClike
  )
