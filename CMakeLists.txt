cmake_minimum_required(VERSION 3.10)
project(psync CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_definitions(-DPSYNC_PLATFORM_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_definitions(-DPSYNC_PLATFORM_WINDOWS)
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
target_include_directories(psync INTERFACE ${CMAKE_SOURCE_DIR}/include)

# Basic test executable
add_executable(psync_test test_main.cc)
target_link_libraries(psync_test psync)

# Torture test executable
add_executable(psync_torture test_torture.cc)
target_link_libraries(psync_torture psync)
target_compile_definitions(psync_torture PRIVATE PSYNC_ENABLE_TELEMETRY=1)

# Minimal Windows threading test
add_executable(test_windows_thread test_windows_thread.cc)

# Minimal Windows sync test
add_executable(test_windows_sync test_windows_sync.cc)
target_link_libraries(test_windows_sync psync)

# Simple torture test
add_executable(test_simple_torture test_simple_torture.cc)
target_link_libraries(test_simple_torture psync)

# Shared mutex test
add_executable(test_shared_mutex test_shared_mutex.cc)
target_link_libraries(test_shared_mutex psync)

# Minimal torture test (direct Windows threading)
add_executable(test_minimal_torture test_minimal_torture.cc)
target_link_libraries(test_minimal_torture psync)
target_compile_definitions(test_minimal_torture PRIVATE PSYNC_ENABLE_TELEMETRY=1)

# Thread abstraction test
add_executable(test_thread_abstraction test_thread_abstraction.cc)
target_link_libraries(test_thread_abstraction psync)

# Torture test step 1 (SharedMutex only)
add_executable(test_torture_step1 test_torture_step1.cc)
target_link_libraries(test_torture_step1 psync)
target_compile_definitions(test_torture_step1 PRIVATE PSYNC_ENABLE_TELEMETRY=1)

# Torture test step 2 (SharedMutex + Batch)
add_executable(test_torture_step2 test_torture_step2.cc)
target_link_libraries(test_torture_step2 psync)
target_compile_definitions(test_torture_step2 PRIVATE PSYNC_ENABLE_TELEMETRY=1)

# Torture test step 3 (SharedMutex + Batch + CondVar)
add_executable(test_torture_step3 test_torture_step3.cc)
target_link_libraries(test_torture_step3 psync)
target_compile_definitions(test_torture_step3 PRIVATE PSYNC_ENABLE_TELEMETRY=1)

# Condition variable only test
add_executable(test_condvar_only test_condvar_only.cc)
target_link_libraries(test_condvar_only psync)

# Final torture test (core primitives only)
add_executable(test_torture_final test_torture_final.cc)
target_link_libraries(test_torture_final psync)
target_compile_definitions(test_torture_final PRIVATE PSYNC_ENABLE_TELEMETRY=1)

# Platform-specific linking
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(psync_test pthread)
    target_link_libraries(psync_torture pthread)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(psync_test)
    target_link_libraries(psync_torture)
endif()

# Enable testing
enable_testing()
add_test(NAME psync_tests COMMAND psync_test)
add_test(NAME psync_torture COMMAND psync_torture)
