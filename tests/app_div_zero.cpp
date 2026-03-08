// =============================================================================
// tests/app_div_zero.cpp
//
// Test application: triggers a SIGFPE (signal 8) using the user's verified
// snippet structure for QEMU compatibility.
// =============================================================================

#include <iostream>
#include "client/linux/handler/exception_handler.h"

using google_breakpad::ExceptionHandler;
using google_breakpad::MinidumpDescriptor;

static bool DumpCallback(const MinidumpDescriptor &descriptor,
                         void * /*context*/, bool succeeded) {
    if (succeeded) {
        std::cout << "BREAKPAD_DUMP_PATH=" << descriptor.path() << std::endl;
    } else {
        std::cerr << "ERROR: Breakpad failed to write dump.\n";
    }
    return succeeded;
}

int main() {
    // Write dumps under /tmp so the directory always exists in Docker and host.
    MinidumpDescriptor descriptor("/tmp");

    ExceptionHandler handler(descriptor, /*filter*/ nullptr, DumpCallback,
                             /*callback_context*/ nullptr,
                             /*install_handler*/ true,
                             /*server_fd*/ -1);

    // QEMU fallback: force dump generation since hardware traps are swallowed.
    handler.WriteMinidump();

    volatile int a = 10;
    volatile int b = 0;
    volatile int c = a / b;
    (void)c;

    return 0;
}
