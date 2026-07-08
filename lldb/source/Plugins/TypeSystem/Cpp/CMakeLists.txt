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
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbUtility
)
