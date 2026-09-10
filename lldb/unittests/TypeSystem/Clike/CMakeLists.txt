add_lldb_unittest(TypeSystemClikeUnitTests
  BuiltinTypesTest.cpp
  ContextTest.cpp
  IdentifierTest.cpp
  LanguageOptsTest.cpp
  NamespaceTest.cpp
  TypeCTest.cpp
  TypeCppTest.cpp
  TypeNameTest.cpp
  TypeObjCTest.cpp
  TypeTest.cpp

  LINK_LIBS
    lldbPluginTypeSystemClike
  )
