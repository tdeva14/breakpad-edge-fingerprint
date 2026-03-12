# minidump_stackwalk_lite

A lightweight, symbol-free Breakpad minidump processor for **crash deduplication at the edge**. It reads a `.dmp` file and emits a single deterministic **crash fingerprint** line to stdout — enabling a device or gateway to identify, count, and suppress duplicate crash reports locally before they reach a central backend, with no symbol server, no network round-trip, and no debug symbols required.

```
SIGNAL+<code>|<BuildID+Frame0>|<BuildID+Frame1>|<BuildID+Frame2>|...
```

---

## Features

- **Edge deduplication** — generate a stable fingerprint on the device itself; suppress duplicates before uploading
- **No symbol server required** — works on stripped production binaries out of the box
- **Full stack walk** — every frame of the crashing thread, not just the top frame
- **Multi-architecture** — processes dumps from and runs on x86, x86\_64, ARM32, and ARM64
- **Lightweight** — single C++14 source file, under 160 lines
- **Portable** — builds with GCC or Clang; CMake automatically handles `libdisasm` availability differences between platforms (see [Portability](#portability))

---

## Architecture & design

### Problem: crash flooding at the edge

Devices in the field repeatedly report the same crash. Without local deduplication, the same stack trace is uploaded thousands of times per day, wasting bandwidth and burying the signal in noise.

### Solution: fingerprint locally, deduplicate before upload

```
┌─────────────────────────────────────────────────────┐
│                    Device / Edge Node               │
│                                                     │
│   Process crashes                                   │
│       │                                             │
│       ▼                                             │
│   Breakpad ExceptionHandler                         │
│       │  writes crash.dmp to /tmp                  │
│       ▼                                             │
│   minidump_stackwalk_lite crash.dmp                 │
│       │  emits: SIGNAL+<code>|<BuildID+Frame0>|...  │
│       ▼                                             │
│   Local dedup store  ──── seen before? ─ YES ─► drop / increment counter
│       │ NO                                          │
│       ▼                                             │
│   Upload crash.dmp + fingerprint to backend        │
└─────────────────────────────────────────────────────┘
```

Because the fingerprint is computed entirely from data inside the `.dmp` file — the module Build-ID, signal number, and per-frame module-relative offsets — it is **stable across process restarts**, even when the OS loads modules at different virtual addresses each run.

### Process flow

```
minidump_stackwalk_lite crash.dmp
│
├─ MinidumpProcessor::Process()        # open and parse the .dmp
│   └─ ProcessState
│       ├─ requesting_thread()         # index of the thread that crashed
│       ├─ exception_record()->code()  # OS signal / exception code
│       └─ threads()[idx]->frames()    # full CallStack for that thread
│           └─ for each StackFrame
│               ├─ module mapped   →  BuildID+basename+0x(instruction − module_base)
│               └─ module unmapped →  UNKNOWN
└─ stdout: SIGNAL+<code>|Frame0|Frame1|...|FrameN
```

**BuildID** — the ELF Build-ID of each frame’s own module, read from the minidump via `code_identifier()`. Every mapped frame carries its module’s Build-ID, so the fingerprint identifies every binary on the crashing stack, not just the innermost one. Remains valid on stripped binaries.

**Per-frame offset** — `instruction_address − module_base_address`. Because this is relative to the module’s own load address, the same crash site produces the same offset on every run, regardless of where the OS placed the module in virtual memory.

**Signal** — the raw OS exception code embedded in the minidump by Breakpad at capture time (e.g. 6 = SIGABRT, 11 = SIGSEGV).

---

## Output format

```
# default
SIGNAL+<code>|<Frame0>|<Frame1>|...|<FrameN>

# with -b
SIGNAL+<code>|<BuildID+Frame0>|<BuildID+Frame1>|...|<BuildID+FrameN>
```

| Field | Type | Description |
|---|---|---|
| `SIGNAL+<code>` | `SIGNAL+` prefix + decimal integer | OS exception code — on Linux this is the signal number (e.g. 11 = SIGSEGV, 6 = SIGABRT) |
| `FrameN` | string | `basename+0xOffset` by default; `<BuildID>+basename+0xOffset` with `-b`; `UNKNOWN` when the module is unmapped |

**Example (default):**
```
SIGNAL+11|myapp+0x4a3c|libc.so.6+0x3b7f0|libpthread.so.0+0x12e0|UNKNOWN
```

**Example (with `-b`):**
```
SIGNAL+11|A3B1C2D4E5F600112233445566778899+myapp+0x4a3c|B4C2D3E4F5A6B7C8D9E0F1A2B3C4D5E6+libc.so.6+0x3b7f0|C5D3E1F2A3B4C5D6E7F8A9B0C1D2E3F4+libpthread.so.0+0x12e0|UNKNOWN
```

---

## Limitations & accuracy

Understanding where this approach is intentionally approximate helps use it correctly.

### No source-level symbolication

Frames are reported as `module+0xOffset`, not `function_name (file.c:42)`. If human-readable stack traces are needed for debugging, run the full `minidump_stackwalk` tool with symbol files (`.sym`) on a backend that has them. `minidump_stackwalk_lite` is optimised for **identity comparison**, not readability.

### Stack-walk quality on stripped binaries

`MinidumpProcessor` uses Breakpad’s stackwalker, which relies on Call Frame Information (CFI) records embedded in the binary. When CFI is absent — common in fully stripped or aggressively optimised binaries — the walker falls back to heuristic stack scanning, which is probabilistic. You may see:

- Fewer frames than expected (walk terminates early when heuristics fail)
- Occasional spurious frames recovered by stack scanning
- `UNKNOWN` frames where the instruction pointer fell in an unmapped region (e.g. a JIT buffer or a corrupted stack)

For deduplication purposes this is acceptable: Frame 0 and Frame 1 are almost always recovered accurately from the CPU context and return-address registers, and those innermost frames are the strongest signal for crash identity.

### One fingerprint per invocation

The tool processes one `.dmp` at a time and exits. Aggregation, counters, and the dedup store are the responsibility of the calling system.

### Linux / ELF only

Build-ID extraction relies on the ELF Build-ID note written by `ld` at link time. Windows PE files use a different identifier scheme. The tool is designed and tested for Linux ELF targets.

---

## Prerequisites

- **C++14 compiler** — GCC 5+ or Clang 3.4+
- **CMake 3.10+**
- **Google Breakpad** — installed with headers at `/usr/local/include/breakpad` and libraries at `/usr/local/lib` (`libbreakpad.a`, `libbreakpad_client.a`)
- **x86/x86\_64 only:** `libdisasm-dev` (`apt install libdisasm-dev`);
  on ARM, CMake uses the bundled stubs automatically — no action needed

---

## Build

### CMake (recommended)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The `minidump_stackwalk_lite` binary is placed in `build/`. CMake automatically selects `libdisasm` or the stub source depending on the host (see [Portability](#portability)).

To also build the test applications:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build
```

### One-liner (scripting / CI)

```bash
# ARM32 / ARM64  —  libdisasm not packaged, use bundled stubs
# Add -b to the minidump_stackwalk_lite invocation to include Build-IDs
g++ -std=c++14 -O2 -I/usr/local/include/breakpad \
    src/minidump_stackwalk_lite.cpp src/disasm_stub.c \
    -L/usr/local/lib -lbreakpad -lpthread -ldl \
    -o minidump_stackwalk_lite

# x86 / x86_64  —  libdisasm-dev installed
g++ -std=c++14 -O2 -I/usr/local/include/breakpad \
    src/minidump_stackwalk_lite.cpp \
    -L/usr/local/lib -lbreakpad -ldisasm -lpthread -ldl \
    -o minidump_stackwalk_lite
```

---

## Usage

```
minidump_stackwalk_lite [-b] <path/to/crash.dmp>
```

One fingerprint line is written to **stdout**; diagnostics go to **stderr**. Exit code is `0` on success, non-zero on failure.

| Option | Description |
|---|---|
| `-b`, `--enable-build-id` | Prefix each frame token with its module’s ELF Build-ID |
| `-h`, `--help` | Print usage and exit |

```bash
# default (no Build-IDs)
$ ./minidump_stackwalk_lite /tmp/c7f1d2a4-crash.dmp
SIGNAL+11|myapp+0x4a3c|libc.so.6+0x3b7f0|UNKNOWN

# with per-frame Build-IDs
$ ./minidump_stackwalk_lite -b /tmp/c7f1d2a4-crash.dmp
SIGNAL+11|A3B1C2D4E5F600112233445566778899+myapp+0x4a3c|B4C2D3E4F5A6B7C8D9E0F1A2B3C4D5E6+libc.so.6+0x3b7f0|UNKNOWN
```

---

## Portability

`minidump_stackwalk_lite` builds and runs identically on all four common Linux ABIs:

| Architecture | Class | Notes |
|---|---|---|
| x86 (IA-32) | 32-bit | Requires `libdisasm-dev` |
| x86\_64 | 64-bit | Requires `libdisasm-dev` |
| ARM (armhf / armv7) | 32-bit | Uses bundled `disasm_stub.c` |
| AArch64 (ARM64) | 64-bit | Uses bundled `disasm_stub.c` |

### Why `disasm_stub.c` exists

Breakpad’s `libbreakpad.a` unconditionally includes `disassembler_x86.o`, which references seven symbols from `libdisasm` (an x86-specific disassembler used by Breakpad’s exploitability engine). On ARM targets `libdisasm` is not available through the system package manager, causing a link failure.

`src/disasm_stub.c` provides empty, `extern "C"`-wrapped implementations of those seven symbols. The stubs are **safe at runtime**: `MinidumpProcessor` is constructed without exploitability analysis (`enable_exploitability = false`), so the x86 disassembler code is never reached on any target.

CMake’s `find_library` chooses the right path at configure time:

```
find_library(LIBDISASM_LIB NAMES disasm)
  found  →  link -ldisasm          (x86 / x86_64)
  absent →  compile disasm_stub.c  (ARM32 / ARM64)
```

---

## Test suite

Under `tests/` are two crash-generating programs exercised by `verify.sh`:

| Binary | Crash type | Signal |
|---|---|---|
| `app_null_ptr` | Null pointer dereference in thread 1 of a 3-thread process | SIGSEGV (11) |
| `app_libc_crash` | Double-free (`free(); free()`) 3 calls deep inside `libcrash_lib.so` | SIGABRT (6) |

Both binaries are stripped after build (`strip --strip-all`) to verify the tool works correctly without debug symbols.

### Running

```bash
bash verify.sh
```

`verify.sh` performs five checks:

1. Compiles all binaries with `g++` (auto-detects `libdisasm` vs. stubs)
2. Runs `app_null_ptr` → validates fingerprint format
3. Runs `app_libc_crash` → validates fingerprint format
4. Runs `app_libc_crash stack` (allocates a 2 KB stack buffer in `level3()`) → validates fingerprint format
5. Runs `app_null_ptr` a second time → asserts Frame 0’s offset is byte-for-byte identical across both runs

Generated `.dmp` files are saved to `tests/minidumps/` as reusable fixtures.

---

## Repository layout

```
src/
  minidump_stackwalk_lite.cpp   # core implementation (~150 lines, C++14)
  disasm_stub.c                 # libdisasm no-op stubs for non-x86 targets
tests/
  app_null_ptr.cpp              # SIGSEGV test (3 threads, crashes thread 1)
  crash_lib.h / crash_lib.cpp   # shared library: trigger_crash → level1 → level2 → level3 → double-free
  app_libc_crash.cpp            # SIGABRT test, links libcrash_lib.so
  minidumps/                    # .dmp fixtures populated by verify.sh
CMakeLists.txt                  # build system (auto-selects libdisasm vs. stubs)
verify.sh                       # end-to-end verification script
requirements.md                 # project requirements
```
