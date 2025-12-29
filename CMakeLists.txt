set(BUILD_SHARED_LIBS OFF)
set(CMAKE_BUILD_TYPE Release)

# Set vcpkg triplet for static libraries on Windows (must be set before project())
if(WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-release" CACHE STRING "")
endif()

cmake_minimum_required(VERSION 3.16)

project(Quark VERSION 1.0.0 LANGUAGES C CXX)

# Force static compilation for all dependencies
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Force static libraries" FORCE)
set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CURL_STATICLIB ON CACHE BOOL "Use static CURL" FORCE)

set(LLVM_ENABLE_DIA_SDK OFF CACHE BOOL "")
set(LLVM_ENABLE_LIBEDIT OFF CACHE BOOL "")
set(LLVM_ENABLE_LIBXML2 OFF CACHE BOOL "")

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force static runtime library for all targets
if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<CONFIG:Release>:>")
    # Set static runtime for all configurations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
endif()
 
if(APPLE)
    set(LLVM_DIR "/opt/homebrew/Cellar/llvm/21.1.0/lib/cmake/llvm")
    set(ixwebsocket_DIR "/opt/homebrew/opt/ixwebsocket/lib/cmake/ixwebsocket")

    find_package(ZSTD REQUIRED CONFIG)
    # Find OpenSSL on macOS so ixwebsocket can use TLS (may be provided via Homebrew)
    find_package(OpenSSL REQUIRED)

    message(STATUS "Found ZSTD ${ZSTD_PACKAGE_VERSION}")
    message(STATUS "Using ZSTDConfig.cmake in: ${ZSTD_DIR}")

endif()

find_package(ftxui CONFIG REQUIRED)
message(STATUS "Found FTXUI ${FTXUI_PACKAGE_VERSION}")
message(STATUS "Using FTXUIConfig.cmake in: ${FTXUI_DIR}")

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

find_package(LLD REQUIRED CONFIG)

message(STATUS "Found LLD ${LLD_PACKAGE_VERSION}")
message(STATUS "Using LLDConfig.cmake in: ${LLD_DIR}")

find_package(CURL REQUIRED)

message(STATUS "Found CURL ${CURL_PACKAGE_VERSION}")
message(STATUS "Using CURLConfig.cmake in: ${CURL_DIR}")

find_package(ixwebsocket CONFIG REQUIRED)

message(STATUS "Found IXWebSocket")
message(STATUS "Using IXWebSocketConfig.cmake in: ${ixwebsocket_DIR}")

if(APPLE)
    list(APPEND LLD_LIBS lldMachO)
elseif(WIN32)
    list(APPEND LLD_LIBS lldCOFF)
    # WindowsManifest requires these components
    list(APPEND EXTRA_COMPONENTS LLVMDebugInfoPDB LLVMLibDriver LLVMWindowsManifest)
else()
    list(APPEND LLD_LIBS lldELF)
endif()

list(APPEND LLD_LIBS lldCommon)

if(LLVM_VERSION_MAJOR LESS_EQUAL 13)
    list(APPEND LLD_LIBS lldDriver)
endif()

# Command line warning D9025 : overriding '/EHs' with '/EHs-'
# prevent LLVM from adding /EHs-c-.
if(WIN32)
    set(LLVM_REQUIRES_EH ON)
endif()

# Set compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS")

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib/json)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib/io)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib/toml)
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${LLD_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})

# Add LLVM definitions
add_definitions(${LLVM_DEFINITIONS})

# Collect source files for the compiler library
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
)
file(GLOB_RECURSE HEADERS
    "include/*.h"
)

list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/quark.cpp")

add_library(quark_compiler STATIC ${SOURCES} ${HEADERS})

# HTTP runtime library (linked into user programs via LLD)
add_library(quark_http STATIC
    lib/http/quark_http.cpp
)

# Link CURL to the HTTP library

target_link_libraries(quark_http PRIVATE CURL::libcurl)

# JSON runtime library (single-header parser writer)
add_library(quark_json STATIC
    lib/json/quark_json.cpp
)
target_include_directories(quark_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/json)

# IO runtime library (file + console helpers)
add_library(quark_io STATIC
    lib/io/quark_io.cpp
)
target_include_directories(quark_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/io)

# TOML runtime library (toml++ powered parser and serializer)
add_library(quark_toml STATIC
    lib/toml/quark_toml.cpp
)
target_include_directories(quark_toml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/toml)

# WebSocket runtime library (IXWebSocket-based client)
add_library(quark_ws STATIC
    lib/ws/quark_ws.cpp
)

target_include_directories(quark_ws PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ws)
target_include_directories(quark_ws PRIVATE /opt/homebrew/opt/ixwebsocket/include)
target_link_libraries(quark_ws PRIVATE ixwebsocket::ixwebsocket)
if(APPLE)
    # Ensure OpenSSL is linked into the websocket static lib so TLS symbols are resolved
    target_link_libraries(quark_ws PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
# Force static linking for IXWebSocket dependencies on Windows
if(WIN32)
    target_compile_definitions(quark_ws PRIVATE IX_USE_STATIC_LIBS)
    target_link_options(quark_ws PRIVATE /SUBSYSTEM:CONSOLE /NODEFAULTLIB:msvcrt.lib)
endif()

# Link with LLVM libraries
set(LLVM_COMPONENTS
    analysis
    core
    executionengine
    mcjit
    interpreter
    instcombine
    object
    orcjit
    support
    runtimedyld
    scalaropts
    transformutils
    windowsmanifest
    target
    ipo
    vectorize
    passes
    coroutines
)

if(APPLE)
    list(APPEND LLVM_COMPONENTS
        aarch64codegen
        aarch64asmparser
        aarch64desc
        aarch64disassembler
        aarch64info
    )
endif()

if(WIN32)
    list(APPEND LLVM_COMPONENTS
        x86codegen
        x86asmparser
        x86desc
        x86disassembler
        x86info
    )
endif()

llvm_map_components_to_libnames(llvm_libs ${LLVM_COMPONENTS})

target_link_libraries(quark_compiler
    PUBLIC
    ${llvm_libs}
    ${LLD_LIBS}
    ${EXTRA_COMPONENTS}
    ixwebsocket::ixwebsocket
    CURL::libcurl
    quark_io
    ftxui::screen
    ftxui::dom
    ftxui::component
)

if(APPLE)
    # Link OpenSSL into the final binary as well to satisfy ixwebsocket TLS usage
    target_link_libraries(quark_compiler PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# Force static compilation for main target
if(WIN32)
    target_compile_definitions(quark_compiler PRIVATE IX_USE_STATIC_LIBS)
    # Explicitly link static runtime for consumers
    target_link_libraries(quark_compiler PUBLIC libcmt.lib)
endif()

add_executable(quark src/quark.cpp)
target_link_libraries(quark PRIVATE quark_compiler)
if(WIN32)
    target_link_options(quark PRIVATE /SUBSYSTEM:CONSOLE /NODEFAULTLIB:msvcrt.lib)
endif()
add_dependencies(quark quark_http quark_json quark_ws quark_toml)

# Copy the runtime static library next to the compiler binary for LLD to consume
add_custom_command(TARGET quark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:quark_http>
    $<TARGET_FILE_DIR:quark>
)

add_custom_command(TARGET quark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:quark_json>
    $<TARGET_FILE_DIR:quark>
)

add_custom_command(TARGET quark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:quark_ws>
    $<TARGET_FILE_DIR:quark>
)

add_custom_command(TARGET quark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:quark_toml>
    $<TARGET_FILE_DIR:quark>
)

add_custom_target(run_tests
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running Quark tests"
)