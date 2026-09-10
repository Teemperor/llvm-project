add_lldb_library(lldbPluginTypeSystemClike PLUGIN
  BuiltinTypes.cpp
  ByteSizedType.cpp
  Identifier.cpp
  LanguageOpts.cpp
  NamedType.cpp
  Type.cpp
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
