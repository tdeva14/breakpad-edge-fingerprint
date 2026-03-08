cmake_minimum_required(VERSION 3.10)

# Fix for CMake 3.16 + QEMU: skip full link during compiler identification
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(edge-fingerprint LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Breakpad installation paths (set by `make install` inside the container) ──
set(BREAKPAD_PREFIX "/usr/local" CACHE PATH "Breakpad install prefix")

# Breakpad installs headers to:
#   /usr/local/include/breakpad/google_breakpad/processor/*.h
#   /usr/local/include/breakpad/client/linux/handler/*.h
# So the include root must be /usr/local/include/breakpad, and source files
# use paths like:  #include "google_breakpad/processor/minidump.h"
set(BREAKPAD_INCLUDE_DIR "${BREAKPAD_PREFIX}/include/breakpad")
set(BREAKPAD_LIB_DIR     "${BREAKPAD_PREFIX}/lib")

# Processor library (for minidump reading / crash-sig-gen)
find_library(BREAKPAD_PROCESSOR_LIB
    NAMES breakpad
    HINTS "${BREAKPAD_LIB_DIR}"
    REQUIRED)

# Client library (for ExceptionHandler in test apps)
find_library(BREAKPAD_CLIENT_LIB
    NAMES breakpad_client
    HINTS "${BREAKPAD_LIB_DIR}"
    REQUIRED)

message(STATUS "Breakpad processor lib : ${BREAKPAD_PROCESSOR_LIB}")
message(STATUS "Breakpad client lib    : ${BREAKPAD_CLIENT_LIB}")
message(STATUS "Breakpad include dir   : ${BREAKPAD_INCLUDE_DIR}")

# ─── crash-sig-gen (the core utility) ─────────────────────────────────────────
add_executable(crash-sig-gen
    src/signature_generator.cpp
)

target_include_directories(crash-sig-gen PRIVATE
    "${BREAKPAD_INCLUDE_DIR}"
)

target_link_libraries(crash-sig-gen PRIVATE
    "${BREAKPAD_PROCESSOR_LIB}"
    pthread
    dl
)

# ─── Test applications (conditional) ──────────────────────────────────────────
option(BUILD_TESTS "Build crash-generating test applications" OFF)

if(BUILD_TESTS)
    message(STATUS "BUILD_TESTS=ON — building test applications")

    # Helper function: create a test app, link it, then strip it
    function(add_crash_test target source)
        add_executable(${target} tests/${source})

        target_include_directories(${target} PRIVATE
            "${BREAKPAD_INCLUDE_DIR}"
        )

        target_link_libraries(${target} PRIVATE
            "${BREAKPAD_CLIENT_LIB}"
            pthread
            dl
        )

        # strip --strip-all   - simulates production RDK (no debug symbols)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND strip --strip-all "$<TARGET_FILE:${target}>"
            COMMENT "Stripping ${target} (strip --strip-all)"
        )
    endfunction()

    add_crash_test(app_null_ptr app_null_ptr.cpp)
    add_crash_test(app_div_zero app_div_zero.cpp)
endif()
