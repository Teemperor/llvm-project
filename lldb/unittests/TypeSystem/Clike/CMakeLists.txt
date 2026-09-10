add_lldb_unittest(TypeSystemClikeUnitTests
  BuiltinTypesTest.cpp
  IdentifierTest.cpp
  LanguageOptsTest.cpp
  TypeObjCTest.cpp
  TypeTest.cpp

  LINK_LIBS
    lldbPluginTypeSystemClike
  )
