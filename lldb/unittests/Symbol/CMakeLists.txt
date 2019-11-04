add_lldb_unittest(SymbolTests
  CompilerTypeTest.cpp
  LocateSymbolFileTest.cpp
  PostfixExpressionTest.cpp
  TestClangASTContext.cpp
  TestDWARFCallFrameInfo.cpp
  TestType.cpp
  TestLineEntry.cpp

  LINK_LIBS
    lldbHost
    lldbSymbol
    lldbUtilityHelpers
    lldbSymbolHelpers
    lldbPluginObjectFileELF
    lldbPluginObjectFileMachO
    lldbPluginSymbolFileDWARF
    lldbPluginSymbolFileSymtab
    LLVMTestingSupport
  )

set(test_inputs
  inlined-functions.yaml
  )
add_unittest_inputs(SymbolTests "${test_inputs}")
