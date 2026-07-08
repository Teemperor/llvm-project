add_lldb_library(lldbPluginTypeSystemCpp PLUGIN
  BuiltinTypes.cpp
  Context.cpp
  Identifier.cpp
  Namespace.cpp
  Records.cpp
  Function.cpp
  Type.cpp
  TypeSystemCpp.cpp

  LINK_COMPONENTS
    Support
    ADT
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginExpressionParserClang
    lldbPluginSymbolFileDWARF
    lldbPluginSymbolFilePDB
    lldbPluginObjCRuntime
  CLANG_LIBS
    clangAST
    clangBasic
    clangFrontend
    clangSema
)
