add_lldb_unittest(TypeSystemClikeUnitTests
  BuiltinTypesTest.cpp
  ByteSizedTypeTest.cpp
  ContextTest.cpp
  IdentifierTest.cpp
  LanguageOptsTest.cpp
  NamedTypeTest.cpp
  TypeTest.cpp

  LINK_LIBS
    lldbPluginTypeSystemClike
  )
