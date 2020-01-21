add_lldb_tool(lldb-test
  FormatUtil.cpp
  lldb-test.cpp
  SystemInitializerTest.cpp

  LINK_LIBS
    lldbBase
    lldbBreakpoint
    lldbCore
    lldbDataFormatters
    lldbExpression
    lldbHost
    lldbInitialization
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbInitializationAllPlugins
    ${host_lib}

  LINK_COMPONENTS
    Support
  )
  
include_directories(${LLDB_SOURCE_DIR}/source)
