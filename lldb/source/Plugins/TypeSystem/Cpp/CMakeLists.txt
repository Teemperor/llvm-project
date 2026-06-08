add_lldb_library(lldbPluginTypeSystemCpp PLUGIN
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
    lldbPluginSymbolFilePDB
    lldbPluginObjCRuntime
  CLANG_LIBS
    clangAST
    clangBasic
    clangFrontend
    clangSema
)
