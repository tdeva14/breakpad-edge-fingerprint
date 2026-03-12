// =============================================================================
// minidump_stackwalk_lite.cpp  —  minidump_stackwalk_lite
//
// A lightweight, symbol-free Breakpad minidump processor. Parses a .dmp file
// and emits a single deterministic crash fingerprint line to stdout:
//
//   SIGNAL+<code>|<Frame0>|<Frame1>|<Frame2>|...
//
// Frame format (default):
//   - Module known:   <basename>+0x<hex_offset>
//   - Module unknown: UNKNOWN
//
// With -b (--enable-build-id):
//   - Module known:   <BuildID>+<basename>+0x<hex_offset>
//
// Portable: builds and runs on x86, x86_64, ARM32, and ARM64.
// No symbol files, no symbol server, no network access required.
// =============================================================================

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <vector>
#include <libgen.h>   // POSIX basename()

// Breakpad Processor headers
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "google_breakpad/processor/call_stack.h"
#include "google_breakpad/processor/stack_frame.h"
#include "google_breakpad/processor/code_module.h"

// ── helpers ──────────────────────────────────────────────────────────────────

// Return the basename of a path as a std::string.
// E.g.  "/usr/lib/libfoo.so.1" → "libfoo.so.1"
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

// Normalise a Breakpad code_identifier() to an upper-case hex string.
// Trims the trailing '0' that Breakpad appends in its 33-char GUID style.
static std::string normalise_build_id(const std::string& raw) {
    std::string id = to_upper(raw);
    while (id.size() > 2 && id.back() == '0' && id.size() == 33) {
        id.pop_back();
    }
    return id.empty() ? "UNKNOWN_BUILD_ID" : id;
}

// ── entry point ──────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    bool enable_build_id = false;
    const char* dmp_path_arg = nullptr;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-b" || arg == "--enable-build-id") {
            enable_build_id = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout <<
                "Usage: minidump_stackwalk_lite [-b] <path-to-minidump.dmp>\n"
                "\n"
                "Options:\n"
                "  -b, --enable-build-id  Prefix each frame with its module's ELF Build-ID\n"
                "  -h, --help             Show this help and exit\n"
                "\n"
                "Output (default):\n"
                "  SIGNAL+<code>|<basename>+0x<offset>|...\n"
                "\n"
                "Output with -b:\n"
                "  SIGNAL+<code>|<BuildID>+<basename>+0x<offset>|...\n";
            return EXIT_SUCCESS;
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n"
                         "Run with --help for details.\n";
            return EXIT_FAILURE;
        } else if (!dmp_path_arg) {
            dmp_path_arg = argv[i];
        } else {
            std::cerr << "Unexpected argument: " << arg << "\n"
                         "Run with --help for details.\n";
            return EXIT_FAILURE;
        }
    }

    if (!dmp_path_arg) {
        std::cerr << "Usage: minidump_stackwalk_lite [-b] <path-to-minidump.dmp>\n"
                     "Run with --help for details.\n";
        return EXIT_FAILURE;
    }

    const std::string dmp_path = dmp_path_arg;

    // ── 1. Process minidump with full stack walking ──────────────────────────
    // Pass nullptr for both SymbolSupplier and SourceLineResolver: stack walk
    // runs without symbol resolution, keeping the tool lightweight.
    google_breakpad::MinidumpProcessor processor(
        static_cast<google_breakpad::SymbolSupplier*>(nullptr),
        static_cast<google_breakpad::SourceLineResolverInterface*>(nullptr));

    google_breakpad::ProcessState process_state;
    google_breakpad::ProcessResult result =
        processor.Process(dmp_path, &process_state);

    if (result != google_breakpad::PROCESS_OK) {
        std::cerr << "ERROR: MinidumpProcessor failed (code " << result
                  << ") for: " << dmp_path << "\n";
        return EXIT_FAILURE;
    }

    // ── 2. Locate the crashed thread ─────────────────────────────────────────
    int thread_idx = process_state.requesting_thread();
    if (thread_idx < 0) thread_idx = 0;

    const std::vector<google_breakpad::CallStack*>* threads =
        process_state.threads();
    if (!threads || thread_idx >= static_cast<int>(threads->size())) {
        std::cerr << "ERROR: No threads available in process state.\n";
        return EXIT_FAILURE;
    }

    const google_breakpad::CallStack* stack = threads->at(thread_idx);
    if (!stack) {
        std::cerr << "ERROR: Null call stack for thread " << thread_idx << "\n";
        return EXIT_FAILURE;
    }

    const std::vector<google_breakpad::StackFrame*>* frames = stack->frames();
    if (!frames || frames->empty()) {
        std::cerr << "ERROR: No frames in the crashed thread's stack.\n";
        return EXIT_FAILURE;
    }

    // ── 3. Extract signal number ─────────────────────────────────────────────
    // On Linux, Breakpad maps the OS signal into the exception record code.
    uint32_t signal_code = 0;
    if (process_state.crashed()) {
        signal_code = process_state.exception_record()->code();
    }

    // ── 4. Build pipe-separated frame list ───────────────────────────────────
    // Each mapped frame is prefixed with the Build-ID of its own module so
    // the fingerprint carries enough information to identify every binary
    // touched by the crashing stack, not just the innermost one.
    std::ostringstream frame_str;
    for (size_t i = 0; i < frames->size(); ++i) {
        if (i > 0) frame_str << "|";
        const google_breakpad::StackFrame* frame = frames->at(i);
        if (!frame || !frame->module) {
            frame_str << "UNKNOWN";
            continue;
        }
        const uint64_t offset =
            frame->instruction - frame->module->base_address();
        if (enable_build_id) {
            frame_str << normalise_build_id(frame->module->code_identifier())
                      << "+";
        }
        frame_str << module_basename(frame->module->code_file())
                  << "+0x" << std::hex << offset;
    }

    // ── 5. Emit Crash DNA ─────────────────────────────────────────────────────
    // Format: SIGNAL+<code>|<Frame0>|<Frame1>|...
    // Each frame: <BuildID>+<basename>+0x<offset>, or UNKNOWN.
    // signal_code is decimal; frame offsets are hex (set inside the loop).
    std::cout << "SIGNAL+" << std::dec << signal_code
              << "|" << frame_str.str()
              << "\n";

    return EXIT_SUCCESS;
}
