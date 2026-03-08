# TRS: Project "Edge-Fingerprinting"

## 1. Project Objective

Develop a surgical, lightweight C++ utility (`crash-sig-gen`) derived from a trimmed-down **Breakpad v2.1.0** `minidump_stackwalk`. The goal is to generate a "Crash DNA" string on-device for local deduplication in **32-bit ARM** environments.

## 2. Technical Requirements

### 2.1 Core Utility (`crash-sig-gen`)

* **Reference Logic:** Derived from `src/processor/minidump_stackwalk.cc` (Breakpad v2.1.0).
* **Primary Architecture:** **ARM 32-bit (armhf)**.
* **Logic (Top-Frame Only):**
1. Load Minidump using `google_breakpad::Minidump`.
2. Access the `MinidumpException` stream to identify the faulting thread.
3. Extract the **Instruction Pointer (IP)** from the CPU context (`MD_CONTEXT_ARM`).
4. Locate the `MinidumpModule` containing that IP.
5. **Calculate Offset:** $IP - ModuleBaseAddress$.
6. **Metadata:** Extract `Build-ID` and the Linux `Signal Code`.


* **Output Format:** `[BuildID]_[Signal]_[ModuleName]_[HexOffset]` (Exactly one line).

### 2.2 Containerized Dev Environment

* **Strategy:** Use a **Linux armhf** container running via QEMU emulation on the macOS host.
* **Base Image:** `arm32v7/ubuntu:20.04`.
* **QEMU Integration:** The `.devcontainer` must register `qemu-user-static` to allow the ARM64 MacBook to run the 32-bit container binaries.
* **Dependencies:**
* Fetch and compile **Google Breakpad v2.1.0** and **LSS** from source.
* Toolchain: `g++`, `cmake`, `make`, `strip`.



### 2.3 Test Applications (`/tests`)

* **Location:** Separate `/tests` directory.
* **Conditional Build:** Enabled via `-DBUILD_TESTS=ON`.
* **Functionality:** Initialize `google_breakpad::ExceptionHandler` to write `.dmp` files to `/tmp/`.
* **Post-Build:** Apply `strip --strip-all` to simulate the production RDK environment (no symbols).
