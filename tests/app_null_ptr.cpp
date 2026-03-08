// =============================================================================
// tests/app_null_ptr.cpp
//
// Test application: triggers a SIGSEGV (signal 11) so that Breakpad records
// a real Linux signal in the minidump. crash-sig-gen should then report
// "11" as the signal field in the Crash DNA.
// =============================================================================

#include <iostream>
#include "client/linux/handler/exception_handler.h"

using google_breakpad::ExceptionHandler;
using google_breakpad::MinidumpDescriptor;

static bool DumpCallback(const MinidumpDescriptor &descriptor,
                         void * /*context*/, bool succeeded) {
    std::cout << "BREAKPAD_DUMP_PATH=" << descriptor.path() << std::endl;
    return succeeded;
}

int main() {
    MinidumpDescriptor descriptor("/tmp");

    ExceptionHandler handler(descriptor,
                             /*filter*/ nullptr,
                             DumpCallback,
                             /*callback_context*/ nullptr,
                             /*install_handler*/ true,
                             /*server_fd*/ -1);

    std::cout << "Triggering SIGSEGV (11) via null pointer access..." << std::endl;

    volatile int *ptr = nullptr;
    volatile int value = *ptr;  // Intentional crash
    (void)value;

    return 0;
}
