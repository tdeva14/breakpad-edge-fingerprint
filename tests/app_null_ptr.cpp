#include <iostream>
#include "client/linux/handler/exception_handler.h"
using google_breakpad::ExceptionHandler;
using google_breakpad::MinidumpDescriptor;

static bool DumpCallback(const MinidumpDescriptor &descriptor, void * /*context*/, bool succeeded) {
    std::cout << "BREAKPAD_DUMP_PATH=" << descriptor.path() << std::endl;
    return succeeded;
}

int main() {
    MinidumpDescriptor descriptor("/tmp");
    ExceptionHandler handler(descriptor, nullptr, DumpCallback, nullptr, true, -1);
    handler.WriteMinidump();
    return 0;
}
