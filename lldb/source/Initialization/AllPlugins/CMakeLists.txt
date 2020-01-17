get_property(LLDB_ALL_PLUGINS GLOBAL PROPERTY LLDB_PLUGINS)

add_lldb_library(lldbInitializationAllPlugins
  SystemInitializerAllPlugins.cpp
  LINK_LIBS
    lldbInitialization
    ${LLDB_ALL_PLUGINS}
  LINK_COMPONENTS
    Support
  )
