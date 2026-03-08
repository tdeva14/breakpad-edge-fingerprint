# crash-sig-gen

A lightweight C++ utility that reads a Breakpad `.dmp` file and emits a single, deterministic **Crash DNA** string for on-device crash deduplication:

```
<BuildID>_<Signal>_<ModuleName>_0x<HexOffset>
```

The code is derived from Breakpad's `minidump_stackwalk`, but only looks at the top frame.

---

## How it works

High-level flow in `src/signature_generator.cpp`:

```
crash-sig-gen
│
├─ Minidump::Read()                      # open .dmp
├─ MinidumpException / MinidumpThread    # faulting thread
├─ MinidumpThread::GetContext()          # CPU context
│   └─ GetProgramCounter()               # IP from CPU-specific context
│      • ARM32: MDRawContextARM.iregs[MD_CONTEXT_ARM_REG_PC]
│      • ARM64: MDRawContextARM64.iregs[MD_CONTEXT_ARM64_REG_PC]
│      • x86:   MDRawContextX86.eip
│      • x86_64:MDRawContextAMD64.rip
├─ MinidumpModuleList                    # find owning module for IP
│   └─ offset = IP − module.base_address # ASLR-stable offset
├─ module.code_identifier()              # Build-ID (hex)
└─ exception_record.exception_code       # Linux signal number
```

The utility runs on any host where Breakpad is installed and can process
minidumps from ARM (32/64-bit) and x86 (32/64-bit) targets.

---

## Build

Prerequisites on the build machine:

- A C++14 compiler (e.g. `g++` or `clang++`).
- Breakpad installed under `/usr/local` (headers in `/usr/local/include/breakpad`,
  libraries in `/usr/local/lib`).

### Simple build with CMake

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces the `crash-sig-gen` binary (and, if `-DBUILD_TESTS=ON`, the test apps).

### One-shot local build (matches verify.sh)

```bash
g++ -std=c++14 -g \
    -I/usr/local/include/breakpad \
    src/signature_generator.cpp \
    -L/usr/local/lib -lbreakpad -lpthread -ldl \
    -o crash-sig-gen
```

---

## Test apps and minidumps

The `/tests` directory contains small crash-generating programs wired to
Breakpad's `ExceptionHandler` to produce minidumps in `/tmp`:

- `tests/app_null_ptr.cpp`  – SIGSEGV-style crash.
- `tests/app_div_zero.cpp`  – SIGFPE-style crash.

Running `verify.sh` will:

1. Compile `crash-sig-gen`, `app_null_ptr`, and `app_div_zero` with `g++`.
2. Run each test app to generate a `.dmp` under `/tmp`.
3. Call `crash-sig-gen` on each dump and validate the output format.
4. Run `app_null_ptr` twice to prove the offset is ASLR-stable.
5. Copy the generated dumps into `tests/minidumps/` as small reusable fixtures
   (`null_ptr_1.dmp`, `null_ptr_2.dmp`, `div_zero_1.dmp`).

To run the verification locally (with Breakpad already installed):

```bash
bash verify.sh
```

---

## Output format

```
<BuildID>_<Signal>_<ModuleName>_0x<HexOffset>
```

- `BuildID`   – ELF Build-ID as hex (from `code_identifier()`).
- `Signal`    – Linux signal number (decimal).
- `ModuleName`– Basename of the crashing binary or shared object.
- `HexOffset` – `IP − module_base_address` in hex, ASLR-stable.

Example:

```
A3B1C2D4E5F6001122334455667788990_11_app_null_ptr_0x1a2c
```

---

## Layout

```text
src/
  signature_generator.cpp   # crash-sig-gen implementation
tests/
  app_null_ptr.cpp          # SIGSEGV test app
  app_div_zero.cpp          # SIGFPE test app
  minidumps/                # small .dmp fixtures populated by verify.sh
CMakeLists.txt               # build targets and optional tests
verify.sh                    # end-to-end verification script
requirements.md              # high-level project requirements
```
