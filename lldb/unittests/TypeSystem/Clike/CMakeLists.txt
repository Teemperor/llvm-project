add_lldb_unittest(TypeSystemClikeUnitTests
  BuiltinTypesTest.cpp
  IdentifierTest.cpp
  LanguageOptsTest.cpp
  TypeCppTest.cpp
  TypeObjCTest.cpp
  TypeTest.cpp

  LINK_LIBS
    lldbPluginTypeSystemClike
  )
