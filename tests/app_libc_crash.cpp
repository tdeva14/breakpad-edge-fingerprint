// =============================================================================
// tests/app_libc_crash.cpp
//
// Test application: links against crash_lib (shared) and calls
// trigger_crash(), which performs a double-free() inside a 3-deep call chain,
// causing SIGABRT (signal 6) inside glibc's free() implementation.
//
// Usage:
//   app_libc_crash          — crash without a 2 KB stack buffer in level3()
//   app_libc_crash stack    — crash with a 2 KB stack buffer in level3()
// =============================================================================

#include <iostream>
#include <string>

#include "client/linux/handler/exception_handler.h"
#include "crash_lib.h"

using google_breakpad::ExceptionHandler;
using google_breakpad::MinidumpDescriptor;

static bool DumpCallback(const MinidumpDescriptor& descriptor,
                         void* /*context*/, bool succeeded) {
    if (succeeded) {
        std::cout << "BREAKPAD_DUMP_PATH=" << descriptor.path() << std::endl;
    } else {
        std::cerr << "ERROR: Breakpad failed to write dump.\n";
    }
    return succeeded;
}

int main(int argc, char** argv) {
    MinidumpDescriptor descriptor("/tmp");

    ExceptionHandler handler(descriptor,
                             /*filter*/ nullptr,
                             DumpCallback,
                             /*callback_context*/ nullptr,
                             /*install_handler*/ true,
                             /*server_fd*/ -1);

    const bool use_stack_alloc = (argc > 1 && std::string(argv[1]) == "stack");

    std::cout << "Calling crash_lib::trigger_crash("
              << (use_stack_alloc ? "stack=yes" : "stack=no")
              << ") — crash via double-free in level3() (SIGABRT=6)..."
              << std::endl;

    trigger_crash(use_stack_alloc ? 1 : 0);

    return 0;
}
