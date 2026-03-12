cmake_minimum_required(VERSION 3.10)

# Skip full-link compiler identification test — required for cross-compilation
# environments where the build host cannot execute the target binary.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(minidump-stackwalk-lite LANGUAGES CXX)

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

# ─── libdisasm (needed by breakpad's disassembler_x86 for exploitability) ─────
# On x86/x86_64: link the real library if present (apt: libdisasm-dev).
# On ARM32/ARM64: library is unavailable; compile no-op stubs instead.
# Using stubs is always safe because we construct MinidumpProcessor without
# exploitability analysis, so the x86 disassembler is never called at runtime.
find_library(LIBDISASM_LIB NAMES disasm)
if(LIBDISASM_LIB)
    message(STATUS "libdisasm found        : ${LIBDISASM_LIB}")
    set(DISASM_SOURCES "")
    set(DISASM_LIBS    "${LIBDISASM_LIB}")
else()
    message(STATUS "libdisasm not found — compiling no-op stubs (non-x86 target)")
    set(DISASM_SOURCES "src/disasm_stub.c")
    set(DISASM_LIBS    "")
endif()

# ─── crash-sig-gen (the core utility) ─────────────────────────────────────────
add_executable(minidump_stackwalk_lite
    src/minidump_stackwalk_lite.cpp
    ${DISASM_SOURCES}
)

target_include_directories(minidump_stackwalk_lite PRIVATE
    "${BREAKPAD_INCLUDE_DIR}"
)

target_link_libraries(minidump_stackwalk_lite PRIVATE
    "${BREAKPAD_PROCESSOR_LIB}"
    ${DISASM_LIBS}
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

        # Strip debug symbols to simulate a production environment.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND strip --strip-all "$<TARGET_FILE:${target}>"
            COMMENT "Stripping ${target} (strip --strip-all)"
        )
    endfunction()

    add_crash_test(app_null_ptr app_null_ptr.cpp)

    # ── crash_lib + app_libc_crash ────────────────────────────────────────────
    # Build crash_lib as a shared library so it gets its own ELF Build-ID.
    # The crash happens inside the library (double-free in level3()) so
    # minidump_stackwalk_lite will report crash_lib's Build-ID and module name.
    add_library(crash_lib SHARED tests/crash_lib.cpp)

    target_include_directories(crash_lib PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )

    add_custom_command(TARGET crash_lib POST_BUILD
        COMMAND strip --strip-all "$<TARGET_FILE:crash_lib>"
        COMMENT "Stripping crash_lib (strip --strip-all)"
    )

    add_executable(app_libc_crash tests/app_libc_crash.cpp)

    target_include_directories(app_libc_crash PRIVATE
        "${BREAKPAD_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )

    target_link_libraries(app_libc_crash PRIVATE
        "${BREAKPAD_CLIENT_LIB}"
        crash_lib
        pthread
        dl
    )

    add_custom_command(TARGET app_libc_crash POST_BUILD
        COMMAND strip --strip-all "$<TARGET_FILE:app_libc_crash>"
        COMMENT "Stripping app_libc_crash (strip --strip-all)"
    )
endif()
