// =============================================================================
// tests/crash_lib.cpp  —  shared library for app_libc_crash
//
// Exposes trigger_crash(), which drives a 3-deep call chain ending in a
// double-free() of the same heap pointer. glibc detects the corruption and
// raises SIGABRT (signal 6).
//
// Call graph:
//   trigger_crash(use_stack_alloc)
//     level1(ptr, use_stack_alloc)
//       level2(ptr, use_stack_alloc)
//         level3(ptr, use_stack_alloc)   ← free(ptr) twice → SIGABRT
// =============================================================================

#include "crash_lib.h"

#include <cstdlib>
#include <cstring>

static void level3(char* ptr, int use_stack_alloc) {
    if (use_stack_alloc) {
        // 2 KB stack buffer when "stack" argument is passed to the test app.
        volatile char stack_buf[2048];
        memset((void*)stack_buf, 0xAB, sizeof(stack_buf));
        (void)stack_buf;
    }

    free(ptr);          // first free — valid
    free(ptr);          // second free — heap corruption → SIGABRT
}

static void level2(char* ptr, int use_stack_alloc) {
    level3(ptr, use_stack_alloc);
}

static void level1(char* ptr, int use_stack_alloc) {
    level2(ptr, use_stack_alloc);
}

void trigger_crash(int use_stack_alloc) {
    char* ptr = static_cast<char*>(malloc(64));
    if (!ptr) return;
    memset(ptr, 0, 64);
    level1(ptr, use_stack_alloc);
}
