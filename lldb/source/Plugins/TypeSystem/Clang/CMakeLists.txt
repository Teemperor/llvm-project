add_lldb_library(lldbPluginTypeSystemClang PLUGIN
  TypeSystemClang.cpp

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
    lldbPluginTypeSystemClike
  CLANG_LIBS
    clangAST
    clangBasic
    clangFrontend
    clangSema
)
