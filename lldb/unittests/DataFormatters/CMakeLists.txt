add_lldb_unittest(DataFormattersTests
  StringPrinterTest.cpp

  LINK_LIBS
    lldbCore
    lldbDataFormatters
    lldbUtility
    lldbUtilityHelpers
    LLVMTestingSupport
  )
