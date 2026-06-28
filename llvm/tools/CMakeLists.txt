# This file will recurse into all subdirectories that contain CMakeLists.txt
# Setting variables that match the pattern LLVM_TOOL_{NAME}_BUILD to Off will
# prevent traversing into a directory.
#
# The only tools that need to be explicitly added are ones that have explicit
# ordering requirements.

# Iterates all the subdirectories to create CMake options to enable/disable
# traversing each directory.
create_llvm_tool_options()

if(NOT LLVM_COMPILER_IS_GCC_COMPATIBLE OR NOT LLVM_LIBC_GPU_BUILD)
  set(LLVM_TOOL_LLVM_GPU_LOADER_BUILD OFF)
endif()

if(NOT LLVM_BUILD_LLVM_DYLIB AND NOT LLVM_BUILD_LLVM_C_DYLIB)
  set(LLVM_TOOL_LLVM_SHLIB_BUILD Off)
endif()

if(NOT LLVM_USE_INTEL_JITEVENTS )
  set(LLVM_TOOL_LLVM_JITLISTENER_BUILD Off)
endif()

if(CYGWIN OR NOT LLVM_ENABLE_PIC)
  set(LLVM_TOOL_LTO_BUILD Off)
endif()

if (LLVM_TOOL_LLVM_DRIVER_BUILD)
  add_llvm_tool(llvm-driver)
endif()

# Add LTO, llvm-ar, llvm-config, and llvm-profdata before clang, ExternalProject
# requires targets specified in DEPENDS to exist before the call to
# ExternalProject_Add.
add_llvm_tool_subdirectory(cache-build-session)
add_llvm_tool_subdirectory(lto)
add_llvm_tool_subdirectory(gold)
add_llvm_tool_subdirectory(llvm-ar)
add_llvm_tool_subdirectory(llvm-config)
add_llvm_tool_subdirectory(llvm-ctxprof-util)
add_llvm_tool_subdirectory(llvm-lto)
add_llvm_tool_subdirectory(llvm-profdata)
add_llvm_tool_subdirectory(llvm-cas-dump)
if(LLVM_CAS_ENABLE_REMOTE_CACHE)
  add_llvm_tool_subdirectory(llvm-remote-cache-test)
else()
  set(LLVM_TOOL_LLVM_REMOTE_CACHE_TEST_BUILD Off)
endif()

# Projects supported via LLVM_EXTERNAL_*_SOURCE_DIR need to be explicitly
# specified.
add_llvm_external_project(lld)
add_llvm_external_project(mlir)
# ClangIR and Flang depend on mlir, lldb and Flang depend on clang, sort them
# accordingly so place them afterwards
add_llvm_external_project(clang)
add_llvm_external_project(flang)

# If Swift is registered as an external project and LLDB's Swift support is
# enabled, Swift must be configured *before* LLDB.  LLDB's Swift-language
# plugin (Plugins/Language/Swift/CMakeLists.txt) calls target_link_libraries()
# against swiftAST, swiftClangImporter, etc., which are CMake targets defined
# by Swift's own CMakeLists.txt.  Processing Swift first makes those targets
# available when LLDB's cmake runs.  The already_processed guard inside
# add_llvm_subdirectory() prevents a second inclusion from the foreach loop
# at the bottom of this file.
#
# cmark must come before Swift because Swift's cmake checks whether "cmark"
# is in LLVM_EXTERNAL_PROJECTS; if it is, Swift expects the cmark targets to
# already be defined when its cmake runs.
if(LLDB_ENABLE_SWIFT_SUPPORT AND LLVM_EXTERNAL_SWIFT_SOURCE_DIR
   AND "swift" IN_LIST LLVM_EXTERNAL_PROJECTS)
  if(LLVM_EXTERNAL_CMARK_SOURCE_DIR AND "cmark" IN_LIST LLVM_EXTERNAL_PROJECTS)
    add_llvm_external_project(cmark)
  endif()

  # Build swift-driver with the host swiftc before adding Swift itself.
  # swift-driver is written in Swift and must be compiled with an already-
  # installed toolchain; it cannot be built as part of the same compiler pass
  # that produces swiftc.  The resulting binary is installed next to swiftc
  # (${CMAKE_BINARY_DIR}/bin/swift-driver) by the ExternalProject INSTALL step.
  # After Swift is added below, swift-frontend is made to depend on
  # swift-driver-prebuilt so Ninja builds it before the compiler is used.
  include(AddLLVMSwiftDriver)
  get_filename_component(_swift_driver_root "${LLVM_EXTERNAL_SWIFT_SOURCE_DIR}" DIRECTORY)
  llvm_add_swift_driver_external_project("${_swift_driver_root}")

  add_llvm_external_project(swift)

  # Wire the dependency: swift-frontend must not be used before swift-driver
  # is built and copied into bin/.
  if(TARGET swift-driver-prebuilt AND TARGET swift-frontend)
    add_dependencies(swift-frontend swift-driver-prebuilt)
  endif()
endif()

add_llvm_external_project(lldb)

# The LLDB Swift tests compile test programs with the just-built swiftc, which
# needs its own stdlib swiftmodule files (build/lib/swift/macosx/) to import
# the Swift standard library.  Those files are produced by swift-stdlib-macosx,
# which is not in check-lldb's dependency chain by default.  Add it so that
# `ninja check-lldb` always builds the stdlib first.
if(TARGET check-lldb AND TARGET swift-stdlib-macosx)
  add_dependencies(check-lldb swift-stdlib-macosx)
endif()
# The LLDB swiftembed test variants compile programs with embedded Swift
# (-enable-experimental-feature Embedded) which links against
# build/lib/swift/embedded/arm64-apple-macos/.  Add the embedded-libraries
# target (which includes embedded Concurrency, Volatile, Synchronization etc.)
# so all embedded stdlib files are built before check-lldb runs the tests.
if(TARGET check-lldb AND TARGET embedded-libraries)
  add_dependencies(check-lldb embedded-libraries)
elseif(TARGET check-lldb AND TARGET embedded-stdlib)
  add_dependencies(check-lldb embedded-stdlib)
endif()
# TestSwiftMacro locates swift-plugin-server relative to the compiler binary.
# Build it before running the LLDB test suite.
if(TARGET check-lldb AND TARGET swift-plugin-server)
  add_dependencies(check-lldb swift-plugin-server)
endif()
add_llvm_external_project(bolt)

# Automatically add remaining sub-directories containing a 'CMakeLists.txt'
# file as external projects.
add_llvm_implicit_projects()

add_llvm_external_project(polly)

# libclc depends on clang
add_llvm_external_project(libclc)

# Add subprojects specified using LLVM_EXTERNAL_PROJECTS
foreach(p ${LLVM_EXTERNAL_PROJECTS})
  add_llvm_external_project(${p})
endforeach(p)

set(LLVM_COMMON_DEPENDS ${LLVM_COMMON_DEPENDS} PARENT_SCOPE)

if (LLVM_TOOL_LLVM_DRIVER_BUILD)
  # This is explicitly added at the end _after_ all tool projects so that it can
  # scrape up tools from other projects into itself.
  add_subdirectory(llvm-driver)
  # This must be here otherwise CMake complains in add_llvm_tool_symlink that
  # it can't add_custom_command that happens after llvm-driver is built because
  # llvm-driver was not created in that directory.
  generate_driver_tool_targets()
endif()
