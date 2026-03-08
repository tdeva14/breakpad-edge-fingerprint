// =============================================================================
// signature_generator.cpp  —  crash-sig-gen
//
// Project: Edge-Fingerprinting (TRS)
// Target:  ARM 32-bit armhf (RDK)
//
// Reads a Breakpad minidump (.dmp) and emits ONE line:
//   <BuildID>_<Signal>_<ModuleName>_0x<HexOffset>
//
// Logic (no full Stackwalker):
//   1. Open minidump via google_breakpad::Minidump
//   2. Get MinidumpException  → faulting thread context
//   3. Extract PC from MD_CONTEXT_ARM (iregs[15])
//   4. Walk MinidumpModuleList to find the owning module
//   5. offset = PC - module.base_address
//   6. Build-ID from module.code_identifier()
//   7. Signal from exception_record.exception_code
// =============================================================================

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
#include <libgen.h>   // POSIX basename()

// Breakpad Processor headers (installed by `make install` inside container)
#include "google_breakpad/processor/minidump.h"
#include "google_breakpad/common/minidump_format.h"

// ── helpers ──────────────────────────────────────────────────────────────────

// Return the basename of a path (without extension) as a std::string.
// E.g.  "/usr/lib/libfoo.so.1" → "libfoo.so.1"
//        "app_null_ptr"         → "app_null_ptr"
static std::string module_basename(const std::string& path) {
    if (path.empty()) return "unknown";
    // mutable copy for POSIX basename (may modify the string)
    std::vector<char> buf(path.begin(), path.end());
    buf.push_back('\0');
    return std::string(::basename(buf.data()));
}

// Upper-case a string in place and return it.
static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

// Convert a raw Build-ID byte blob to an upper-case hex string.
// Breakpad encodes the ELF Build-ID in code_identifier() as a HEX string
// already — we just normalise capitalisation.
static std::string normalise_build_id(const std::string& raw) {
    // code_identifier() already returns hex; just upper-case it.
    std::string id = to_upper(raw);
    // Strip any trailing '0' padding that Breakpad appends for PE files
    // (on Linux ELF this is usually already clean, but defensive trim).
    while (id.size() > 2 && id.back() == '0' &&
           // Only trim if it looks like Breakpad's 33-char GUID style
           id.size() == 33) {
        id.pop_back();
    }
    return id.empty() ? "UNKNOWN_BUILD_ID" : id;
}

// ── entry point ──────────────────────────────────────────────────────────────

// Helper to get Signal Number (from Exception Stream)
int GetSignalNumber(google_breakpad::MinidumpException* exception) {
    if (!exception || !exception->exception()) return 0;
    return exception->exception()->exception_record.exception_code;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: crash-sig-gen <path-to-minidump.dmp>\n";
        return EXIT_FAILURE;
    }

    const std::string dmp_path = argv[1];

    // ── 1. Open minidump ─────────────────────────────────────────────────────
    google_breakpad::Minidump dump(dmp_path);
    if (!dump.Read()) {
        std::cerr << "ERROR: Failed to read minidump: " << dmp_path << "\n";
        return EXIT_FAILURE;
    }

    // ── 2. Get the exception stream ──────────────────────────────────────────
    google_breakpad::MinidumpException* exception = dump.GetException();
    int sig_num = GetSignalNumber(exception);

    // ── 3. Get the faulting thread & its CPU context ─────────────────────────
    uint32_t faulting_tid = 0;
    google_breakpad::MinidumpThread* thread = nullptr;

    if (exception) {
        if (!exception->GetThreadID(&faulting_tid)) {
            std::cerr << "Error: Could not get faulting thread ID from exception stream.\n";
            return EXIT_FAILURE;
        }
        sig_num = exception->exception()->exception_record.exception_code;
        
        google_breakpad::MinidumpThreadList* thread_list = dump.GetThreadList();
        if (thread_list) {
            thread = thread_list->GetThreadByID(faulting_tid);
        }
    } else {
        std::cerr << "Warning: No exception stream found. Falling back to first thread.\n";
        google_breakpad::MinidumpThreadList* thread_list = dump.GetThreadList();
        if (thread_list && thread_list->thread_count() > 0) {
            thread = thread_list->GetThreadAtIndex(0);
            if (thread && thread->thread()) {
                faulting_tid = thread->thread()->thread_id;
            }
        }
    }

    if (!thread) {
        std::cerr << "Error: Could not find any thread to process.\n";
        return EXIT_FAILURE;
    }

    google_breakpad::MinidumpContext* ctx = thread->GetContext();
    if (!ctx) {
        std::cerr << "Error: Faulting thread has no CPU context.\n";
        return EXIT_FAILURE;
    }

    // ── 4. Extract the Program Counter for ARM 32-bit ────────────────────────
    //  MD_CONTEXT_ARM context type; PC lives in iregs[15] (MD_CONTEXT_ARM_REG_PC = 15)
    if (ctx->GetContextCPU() != MD_CONTEXT_ARM) {
        std::cerr << "Error: Unexpected CPU context (expected ARM 0x" << std::hex << MD_CONTEXT_ARM
                  << ", got 0x" << ctx->GetContextCPU() << std::dec << ").\n";
        return EXIT_FAILURE;
    }

    const MDRawContextARM* raw_ctx = ctx->GetContextARM();
    if (!raw_ctx) {
        std::cerr << "ERROR: GetContextARM() returned null.\n";
        return EXIT_FAILURE;
    }

    // ARM 32-bit: register 15 is the Program Counter (PC / Instruction Pointer)
    const uint32_t ip = raw_ctx->iregs[MD_CONTEXT_ARM_REG_PC];

    // ── 5. Walk module list to find the owning module ────────────────────────
    google_breakpad::MinidumpModuleList* module_list = dump.GetModuleList();
    if (!module_list) {
        std::cerr << "ERROR: No module list in minidump.\n";
        return EXIT_FAILURE;
    }

    const google_breakpad::MinidumpModule* faulting_module = nullptr;
    for (unsigned int i = 0; i < module_list->module_count(); ++i) {
        const google_breakpad::MinidumpModule* mod = module_list->GetModuleAtIndex(i);
        if (!mod) continue;

        const uint64_t base = mod->base_address();
        const uint64_t size = mod->size();

        if (static_cast<uint64_t>(ip) >= base &&
            static_cast<uint64_t>(ip) <  base + size) {
            faulting_module = mod;
            break;
        }
    }

    if (!faulting_module) {
        std::cerr << "ERROR: Could not map IP 0x" << std::hex << ip
                  << " to any loaded module.\n";
        return EXIT_FAILURE;
    }

    // ── 6. Compute ASLR-stable relative offset ───────────────────────────────
    const uint64_t base_addr = faulting_module->base_address();
    const uint64_t offset    = static_cast<uint64_t>(ip) - base_addr;

    // ── 7. Extract Build-ID ───────────────────────────────────────────────────
    // code_identifier() returns the ELF Build-ID as a hex string on Linux.
    const std::string build_id = normalise_build_id(
        faulting_module->code_identifier());

    // ── 8. Extract Linux signal number ───────────────────────────────────────
    // On Linux, Breakpad maps signal numbers into exception_record.exception_code.
    const MDRawExceptionStream* exc_stream = exception->exception();
    if (!exc_stream) {
        std::cerr << "ERROR: Could not read exception record.\n";
        return EXIT_FAILURE;
    }
    const uint32_t signal_code = exc_stream->exception_record.exception_code;

    // ── 9. Module name (basename only) ───────────────────────────────────────
    const std::string module_name = module_basename(faulting_module->code_file());

    // ── 10. Emit Crash DNA ────────────────────────────────────────────────────
    // Format: [BuildID]_[Signal]_[ModuleName]_0x[HexOffset]
    // IMPORTANT: signal_code is always decimal (Linux signal number),
    // offset is hex.  Use explicit manipulators so stream state is unambiguous.
    std::cout << build_id
              << "_" << std::dec << signal_code
              << "_" << module_name
              << "_0x" << std::hex << offset
              << "\n";

    return EXIT_SUCCESS;
}
