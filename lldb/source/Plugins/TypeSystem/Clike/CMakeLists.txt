add_lldb_library(lldbPluginTypeSystemClike PLUGIN
  Builder.cpp
  BuiltinTypes.cpp
  Context.cpp
  Identifier.cpp
  LanguageOpts.cpp
  ScratchTypeSystemClike.cpp
  Type.cpp
  TypeName.cpp
  TypeSystemClike.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginSymbolFileDWARF
  CLANG_LIBS
    clangBasic
)
