add_lldb_library(lldbPluginTypeSystemClike PLUGIN
  BuiltinTypes.cpp
  Context.cpp
  Identifier.cpp
  LanguageOpts.cpp
  Namespace.cpp
  RecordType.cpp
  SugarType.cpp
  Type.cpp
  TypeC.cpp
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
