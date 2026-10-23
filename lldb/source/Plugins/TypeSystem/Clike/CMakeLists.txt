add_lldb_library(lldbPluginTypeSystemClike PLUGIN
  Builder.cpp
  BuiltinTypes.cpp
  Context.cpp
  Identifier.cpp
  LanguageOpts.cpp
  Namespace.cpp
  RecordType.cpp
  SugarType.cpp
  Type.cpp
  TypeC.cpp
  TypeName.cpp
  TypeSystemClike.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbUtility
  CLANG_LIBS
    clangBasic
)
