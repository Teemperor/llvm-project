add_lldb_library(lldbPluginTypeSystemClike PLUGIN
  BuiltinTypes.cpp
  ByteSizedType.cpp
  Context.cpp
  Identifier.cpp
  LanguageOpts.cpp
  NamedType.cpp
  Namespace.cpp
  RecordType.cpp
  SugarType.cpp
  Type.cpp
  TypeC.cpp
  TypeCpp.cpp
  TypeName.cpp
  TypeObjC.cpp
  TypeSystemClike.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbUtility
  CLANG_LIBS
    clangBasic
)
