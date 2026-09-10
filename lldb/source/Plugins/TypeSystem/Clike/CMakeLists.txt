add_lldb_library(lldbPluginTypeSystemClike PLUGIN
  BuiltinTypes.cpp
  Identifier.cpp
  LanguageOpts.cpp
  Type.cpp
  TypeCpp.cpp
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
