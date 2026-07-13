add_lldb_library(lldbPluginTypeSystemCpp PLUGIN
  Builder.cpp
  BuiltinTypes.cpp
  Context.cpp
  Identifier.cpp
  LanguageOpts.cpp
  Namespace.cpp
  Function.cpp
  Type.cpp
  TypeSystemCpp.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginExpressionParserClang
    lldbPluginSymbolFileDWARF
  CLANG_LIBS
    clangBasic
)
