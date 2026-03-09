// =============================================================================
// tests/app_null_ptr.cpp
//
// Multithreaded test application: spawns 3 threads and triggers a SIGSEGV
// (signal 11) in exactly one of them so that Breakpad records a real Linux
// signal in the minidump for a specific crashing thread.
// =============================================================================

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

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

    std::cout << "Starting 3 threads; only thread #1 will crash with SIGSEGV (11)..." << std::endl;

    auto worker = [](int id) {
        std::cout << "[thread " << id << "] started" << std::endl;

        if (id == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "[thread " << id << "] Triggering SIGSEGV (11) via null pointer access..." << std::endl;

            volatile int *ptr = nullptr;
            volatile int value = *ptr;  // Intentional crash in this thread
            (void)value;
        } else {
            // Busy-wait gently so the process stays alive until the crash.
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(3);
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto &t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}
