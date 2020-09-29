add_lldb_unittest(TargetTests
  ABITest.cpp
  ExecutionContextTest.cpp
  MemoryRegionInfoTest.cpp
  ModuleCacheTest.cpp
  PathMappingListTest.cpp
  RemoteAwarePlatformTest.cpp
  StackFrameRecognizerTest.cpp

  LINK_LIBS
      lldbCore
      lldbHost
      lldbPluginObjectFileELF
      lldbPluginPlatformLinux
      lldbPluginSymbolFileSymtab
      lldbPluginABIAArch64
      lldbTarget
      lldbSymbol
      lldbUtility
      lldbUtilityHelpers
    LINK_COMPONENTS
      Support
      ${LLVM_TARGETS_TO_BUILD}
  )

add_unittest_inputs(TargetTests TestModule.so)
