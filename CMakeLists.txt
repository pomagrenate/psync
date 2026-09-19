cmake_minimum_required(VERSION 3.10)
project(psync CXX)

option(PSYNC_BUILD_TESTS "Build psync tests" ON)
option(PSYNC_BUILD_EXAMPLES "Build psync examples" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_definitions(-DPSYNC_PLATFORM_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_definitions(-DPSYNC_PLATFORM_WINDOWS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_definitions(-DPSYNC_PLATFORM_DARWIN)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /O2")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# psync library (header-only)
add_library(psync INTERFACE)
target_include_directories(psync INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(PSYNC_BUILD_TESTS)
# Basic test executable
add_executable(psync_test test_main.cc)
target_link_libraries(psync_test psync)

# Platform-specific linking
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(psync_test pthread)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(psync_test)
endif()

# Enable testing
enable_testing()
add_test(NAME psync_tests COMMAND psync_test)
endif()
