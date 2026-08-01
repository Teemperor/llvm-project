# cmake initial-cache file for a single-directory LLDB + Swift build.
#
# Usage (from the repo root, i.e. the directory that contains llvm-project/):
#
#   cmake -G Ninja \
#         -C llvm-project/llvm/cmake/caches/swift-lldb.cmake \
#         -S llvm-project/llvm \
#         -B build
#   cmake --build build --target check-lldb
#
# All variables can be overridden on the cmake command line with -D<VAR>=<val>.

# ---------------------------------------------------------------------------
# Clang is quired by LLDB.
# ---------------------------------------------------------------------------
set(LLVM_ENABLE_PROJECTS "clang;lldb" CACHE STRING "")
# compiler-rt provides the LLVM asan/tsan runtime dylibs.
set(LLVM_ENABLE_RUNTIMES "libcxx;libcxxabi;libunwind;compiler-rt" CACHE STRING "" FORCE)
# Only build macOS sanitizer dylibs.
# FIXME: Support also building the other dylibs.
set(COMPILER_RT_ENABLE_IOS     FALSE CACHE BOOL "" FORCE)
set(COMPILER_RT_ENABLE_TVOS    FALSE CACHE BOOL "" FORCE)
set(COMPILER_RT_ENABLE_WATCHOS FALSE CACHE BOOL "" FORCE)
# Only build the sanitizers needed by the LLDB test suite.
set(COMPILER_RT_SANITIZERS_TO_BUILD "asan;tsan" CACHE STRING "" FORCE)

get_filename_component(_swift_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

# swift-syntax is required for building some Swift sources.
if(EXISTS "${_swift_root}/swift-syntax")
  set(SWIFT_BUILD_SWIFT_SYNTAX          ON                            CACHE BOOL "" FORCE)
  set(SWIFT_PATH_TO_SWIFT_SYNTAX_SOURCE "${_swift_root}/swift-syntax" CACHE PATH "" FORCE)
endif()

set(LLDB_DEP_EXTERNAL_PROJECTS     "cmark;swift"           CACHE STRING "" FORCE)
set(LLVM_EXTERNAL_PROJECTS         "${LLDB_DEP_EXTERNAL_PROJECTS}"           CACHE STRING "" FORCE)
set(LLVM_EXTERNAL_CMARK_SOURCE_DIR "${_swift_root}/cmark"   CACHE PATH   "" FORCE)
set(LLVM_EXTERNAL_SWIFT_SOURCE_DIR "${_swift_root}/swift"   CACHE PATH   "" FORCE)

# ---------------------------------------------------------------------------
# LLDB: enable the Swift support.
# ---------------------------------------------------------------------------
set(LLDB_ENABLE_SWIFT_SUPPORT ON CACHE BOOL "")

# ---------------------------------------------------------------------------
# Swift stdlib features — mirror the build-script defaults (all ON).
# These are required for the LLDB Swift test suite to pass.
# ---------------------------------------------------------------------------
set(SWIFT_ENABLE_EXPERIMENTAL_CONCURRENCY   ON CACHE BOOL "")
set(SWIFT_ENABLE_EXPERIMENTAL_DISTRIBUTED   ON CACHE BOOL "")
set(SWIFT_ENABLE_EXPERIMENTAL_OBSERVATION   ON CACHE BOOL "")
set(SWIFT_ENABLE_SYNCHRONIZATION            ON CACHE BOOL "")
set(SWIFT_ENABLE_VOLATILE                   ON CACHE BOOL "")
set(SWIFT_ENABLE_RUNTIME_MODULE             ON CACHE BOOL "")
# String processing sources live in swift-experimental-string-processing/.
if(EXISTS "${_swift_root}/swift-experimental-string-processing")
  set(SWIFT_ENABLE_EXPERIMENTAL_STRING_PROCESSING ON CACHE BOOL "")
  set(SWIFT_PATH_TO_STRING_PROCESSING_SOURCE
      "${_swift_root}/swift-experimental-string-processing" CACHE PATH "")
endif()

# ---------------------------------------------------------------------------
# Faster iteration defaults (override any of these on the command line)
# ---------------------------------------------------------------------------
# Only build the macOS SDK for the host architecture.
set(SWIFT_SDKS                   "OSX"   CACHE STRING "")
set(SWIFT_DARWIN_SUPPORTED_ARCHS "arm64" CACHE STRING "" FORCE)

# Build the C/ObjC/C++ stdlib with the host compiler (avoids missing
# compiler-rt builtins), but route Swift stdlib sources through the
# just-built swiftc — matching build-script's build-runtime-with-host-compiler=0.
set(SWIFT_BUILD_RUNTIME_WITH_HOST_COMPILER ON  CACHE BOOL "" FORCE)
set(SWIFT_NATIVE_SWIFT_TOOLS_PATH "${CMAKE_BINARY_DIR}/bin" CACHE PATH "" FORCE)
# SWIFT_NATIVE_LLVM_TOOLS_PATH is used to locate llvm-ar when building the
# embedded stdlib static archives for non-Darwin platforms under macOS.
# Without it the archiver path is invalid and those .a files are never built,
# which breaks the test_clang_swiftembed LLDB test variants.
set(SWIFT_NATIVE_LLVM_TOOLS_PATH "${CMAKE_BINARY_DIR}/bin" CACHE PATH "" FORCE)
# Enable stdlib assertions to match the build-script's ReleaseAssert configuration.
set(SWIFT_STDLIB_ASSERTIONS ON CACHE BOOL "" FORCE)

# Force debug information for the stdlib as that's required for the tests.
set(SWIFT_STDLIB_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)

# LLDB tests also require back deployment support.
set(SWIFT_STDLIB_SUPPORT_BACK_DEPLOYMENT ON CACHE BOOL "" FORCE)

# Make the just-built swiftc aware of clang sanitizer runtime libraries.
# swiftc checks for sanitizer runtime libs in <swift-resource-dir>/../clang/lib/darwin/
# before allowing -sanitize=address/-sanitize=thread.
# compiler-rt (added to LLVM_ENABLE_RUNTIMES above) builds and installs these
# libs into build/lib/clang/<ver>/lib/darwin/ — the same path that
# build/lib/swift/clang (a symlink → build/lib/clang/<ver>) resolves to.
# On fresh builds the libs don't exist until compiler-rt is built, but by the
# time check-lldb runs, the runtimes have already been compiled.
#
# Remove any stale Xcode symlink at lib/clang/<ver>/lib so compiler-rt can
# install real files there instead of trying to write into Xcode (read-only).
if(APPLE)
  file(GLOB _swift_clang_ver_dirs "${CMAKE_BINARY_DIR}/lib/clang/[0-9]*")
  foreach(_swift_clang_ver_dir ${_swift_clang_ver_dirs})
    if(IS_SYMLINK "${_swift_clang_ver_dir}/lib")
      file(REMOVE "${_swift_clang_ver_dir}/lib")
    endif()
  endforeach()
endif()


# `swift` and `swiftc` in the build tree are just symlinks to swift-frontend,
# which forwards to a `swift-driver` binary sitting next to it. That binary is
# not built by this CMake tree: build-script builds it *before* configuring
# swift (the "EarlySwiftDriver" product) and points
# SWIFT_EARLY_SWIFT_DRIVER_BUILD at the result, which
# swift_create_early_driver_copies() then copies into bin/.
option(LLDB_BUILD_EARLY_SWIFT_DRIVER "Build the early swift-driver for the build-tree swiftc" ON)

# Note: SWIFT_EARLY_SWIFT_DRIVER_BUILD is declared (empty) as a cache variable
# by swift's own CMake, so test it for emptiness rather than for definedness --
# otherwise this is skipped on every reconfigure of an existing build dir.
if(LLDB_BUILD_EARLY_SWIFT_DRIVER AND NOT SWIFT_EARLY_SWIFT_DRIVER_BUILD
   AND EXISTS "${_swift_root}/swift-driver")
  # The helper needs a toolchain root with bin/swiftc and bin/swift in it. The
  # SDK-matching toolchain derived below may not exist, so ask xcrun.
  if(APPLE)
    execute_process(COMMAND xcrun -f swiftc
      OUTPUT_VARIABLE _early_driver_swiftc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  else()
    find_program(_early_driver_swiftc swiftc)
  endif()

  if(CMAKE_MAKE_PROGRAM)
    set(_early_driver_ninja "${CMAKE_MAKE_PROGRAM}")
  else()
    find_program(_early_driver_ninja ninja)
  endif()

  if(_early_driver_swiftc AND _early_driver_ninja)
    get_filename_component(_early_driver_toolchain "${_early_driver_swiftc}" DIRECTORY)
    get_filename_component(_early_driver_toolchain "${_early_driver_toolchain}" DIRECTORY)
    set(_early_driver_build "${CMAKE_BINARY_DIR}/earlyswiftdriver")

    message(STATUS "Building early swift-driver with ${_early_driver_swiftc}")
    execute_process(
      COMMAND "${_swift_root}/swift-driver/Utilities/build-script-helper.py" build
              --package-path "${_swift_root}/swift-driver"
              --build-path   "${_early_driver_build}"
              --configuration release
              --toolchain    "${_early_driver_toolchain}"
              --ninja-bin    "${_early_driver_ninja}"
              --cmake-bin    "${CMAKE_COMMAND}"
              --local_compiler_build
      RESULT_VARIABLE _early_driver_result)

    if(_early_driver_result EQUAL 0)
      set(SWIFT_EARLY_SWIFT_DRIVER_BUILD "${_early_driver_build}/release/bin"
          CACHE PATH "" FORCE)
    else()
      message(FATAL_ERROR "Failed to build the early swift-driver.")
    endif()
  else()
    message(FATAL_ERROR "Could not locate swiftc/ninja to build the early swift-driver.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Swift compiler detection
# On macOS the default xcrun toolchain may return a swiftc that is a different
# version from the SDK; derive the SDK-matching toolchain explicitly.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_Swift_COMPILER)
  execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE _swift_cache_sdk  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(
    COMMAND xcode-select -p
    OUTPUT_VARIABLE _swift_cache_xdev OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_swift_cache_sdk MATCHES "MacOSX([0-9]+\\.[0-9]+)\\.sdk")
    set(_swift_cache_swiftc
        "${_swift_cache_xdev}/Toolchains/OSX${CMAKE_MATCH_1}.xctoolchain/usr/bin/swiftc")
    if(EXISTS "${_swift_cache_swiftc}")
      set(CMAKE_Swift_COMPILER        "${_swift_cache_swiftc}" CACHE FILEPATH "" FORCE)
      set(CMAKE_OSX_SYSROOT           "${_swift_cache_sdk}"    CACHE STRING   "" FORCE)
      set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0"                   CACHE STRING   "" FORCE)
    endif()
  endif()
endif()
