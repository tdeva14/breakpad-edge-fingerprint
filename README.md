# Edge-Fingerprinting — `crash-sig-gen`

> **TRS Project** · 32-bit ARM (armhf) RDK · Breakpad v2.1.0

A surgical, lightweight C++ utility that reads a Breakpad `.dmp` minidump and
emits a single deterministic **Crash DNA** string for on-device deduplication:

```
<BuildID>_<Signal>_<ModuleName>_0x<HexOffset>
```

---

## Table of Contents

1. [Architecture](#architecture)
2. [Prerequisites — Host Setup (QEMU)](#prerequisites--host-setup-qemu)
3. [Build the Dev Container](#build-the-dev-container)
4. [Build the Project](#build-the-project)
5. [Run Verification](#run-verification)
6. [Output Format](#output-format)
7. [ASLR Stability Proof](#aslr-stability-proof)
8. [Project Structure](#project-structure)

---

## Architecture

```
crash-sig-gen  (crash DNA generator)
│
├── google_breakpad::Minidump::Read()       — open .dmp file
├── MinidumpException::GetThread()          — faulting thread
├── MinidumpThread::GetContext()            — CPU register state
│   └── MDRawContextARM.iregs[15]          — PC (no Stackwalker class used)
├── MinidumpModuleList  iteration           — find owning module
│   └── offset = PC − module.base_address  — ASLR-stable
├── module.code_identifier()               — GNU ELF Build-ID
└── exception_record.exception_code        — Linux signal number
```

---

## Prerequisites — Host Setup (QEMU)

Run **once** on your ARM64 macOS host to register QEMU binary translators so
Docker can execute 32-bit ARM (armhf) containers:

```bash
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
```

Verify QEMU is active:

```bash
docker run --rm --platform linux/arm/v7 arm32v7/ubuntu:20.04 uname -m
# Expected output: armv7l
```

---

## Build the Dev Container

```bash
# From the repository root
docker build \
  --platform linux/arm/v7 \
  -t edge-fp \
  .devcontainer/
```

> **Note:** The first build downloads and compiles Breakpad from source inside
> the 32-bit container — this takes ~10–15 minutes on the first run. Subsequent
> builds use Docker's layer cache.

---

## Build the Project

### Option A — VS Code Dev Containers

Open the repository in VS Code and select **"Reopen in Container"** from the
command palette. The devcontainer will build automatically.

### Option B — Manual Docker

```bash
# Without test apps
docker run --rm \
  --platform linux/arm/v7 \
  -v "$(pwd):/work" \
  edge-fp \
  bash -c "cmake -S /work -B /work/build -DCMAKE_BUILD_TYPE=Release && \
           cmake --build /work/build"

# With test apps
docker run --rm \
  --platform linux/arm/v7 \
  -v "$(pwd):/work" \
  edge-fp \
  bash -c "cmake -S /work -B /work/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON && \
           cmake --build /work/build"
```

---

## Run Verification

```bash
docker run --rm \
  --platform linux/arm/v7 \
  -v "$(pwd):/work" \
  edge-fp \
  bash /work/verify.sh
```

---

## Output Format

```
<BuildID>_<Signal>_<ModuleName>_0x<HexOffset>
```

| Field        | Description                                            | Example           |
|:-------------|:-------------------------------------------------------|:------------------|
| `BuildID`    | GNU ELF Build ID (hex, from `.note.gnu.build-id`)      | `A3B1C2D4E5F6…`   |
| `Signal`     | Linux signal number (decimal)                          | `11` (SIGSEGV)    |
| `ModuleName` | Basename of the crashing binary/library                | `app_null_ptr`    |
| `HexOffset`  | `PC − module_base` in hex  (ASLR-stable)               | `0x1a2b`          |

### Example output

```
A3B1C2D4E5F6001122334455667788990_11_app_null_ptr_0x1a2c
B9F2E3C1D4A5881100223344556677880_8_app_div_zero_0x203e
```

---

## ASLR Stability Proof

Even though Linux ASLR randomises load addresses, the **relative offset**
`PC − base_address` stays constant for the same crash site:

```
Run 1:  base=0x10000  PC=0x11a2c  → offset=0x1a2c
Run 2:  base=0x40000  PC=0x41a2c  → offset=0x1a2c   ✓ identical
```

`verify.sh` automatically runs `app_null_ptr` twice and asserts the offset
field is the same in both Crash DNA strings.

---

## Project Structure

```
breakpad-edge-fingerprint/
├── .devcontainer/
│   ├── Dockerfile          # arm32v7/ubuntu:20.04, Breakpad compiled from source
│   └── devcontainer.json   # VS Code Remote Containers config
├── src/
│   └── signature_generator.cpp  # crash-sig-gen core (no Stackwalker)
├── tests/
│   ├── app_null_ptr.cpp    # SIGSEGV test app
│   └── app_div_zero.cpp    # SIGFPE  test app
├── CMakeLists.txt          # BUILD_TESTS=ON for test apps; strip post-build
├── verify.sh               # End-to-end verification script
└── requirements.md
```
