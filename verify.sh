#!/usr/bin/env bash
# =============================================================================
# verify.sh  —  Edge-Fingerprinting end-to-end verification
#
# Test apps:
#   app_null_ptr         — SIGSEGV (11) via null dereference, 3 threads
#   app_libc_crash       — SIGABRT (6)  via double-free in crash_lib
#   app_libc_crash stack — same, but level3() also allocates a 2K stack buffer
#
# Steps:
#   1. Build all binaries with g++
#   2. Run each crash app, collect the .dmp, feed it to crash-sig-gen
#   3. Validate Crash DNA format for each test
#   4. Run app_null_ptr twice to prove the offset is ASLR-stable
#
# Run locally (with Breakpad installed under /usr/local):
#   bash verify.sh
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MINIDUMP_DIR="${SCRIPT_DIR}/tests/minidumps"

# ANSI colours
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

pass()   { echo -e "${GREEN}[PASS]${RESET} $*"; }
fail()   { echo -e "${RED}[FAIL]${RESET} $*"; FAILURES=$((FAILURES + 1)); }
info()   { echo -e "${CYAN}[INFO]${RESET} $*"; }
banner() { echo -e "\n${BOLD}${YELLOW}══ $* ══${RESET}"; }

FAILURES=0

# ─── Step 1: Build ───────────────────────────────────────────────────────────
banner "Step 1: Direct Compilation (g++)"

info "Compiling Core Utility: crash-sig-gen…"
g++ -std=c++14 -g \
    -I/usr/local/include/breakpad \
    "${SCRIPT_DIR}/src/signature_generator.cpp" \
    -L/usr/local/lib -lbreakpad -lpthread -ldl \
    -o "crash-sig-gen"

info "Compiling Test App: app_null_ptr…"
g++ -std=c++14 -g \
    -I/usr/local/include/breakpad \
    -I"${SCRIPT_DIR}/tests" \
    "${SCRIPT_DIR}/tests/app_null_ptr.cpp" \
    -L/usr/local/lib -lbreakpad_client -lpthread -ldl \
    -o "app_null_ptr"
strip --strip-all "app_null_ptr"

info "Compiling crash_lib (shared)…"
g++ -std=c++14 -g -fPIC -shared \
    -I"${SCRIPT_DIR}/tests" \
    "${SCRIPT_DIR}/tests/crash_lib.cpp" \
    -o "libcrash_lib.so"
strip --strip-all "libcrash_lib.so"

info "Compiling Test App: app_libc_crash…"
g++ -std=c++14 -g \
    -I/usr/local/include/breakpad \
    -I"${SCRIPT_DIR}/tests" \
    "${SCRIPT_DIR}/tests/app_libc_crash.cpp" \
    -L/usr/local/lib -lbreakpad_client \
    -L. -lcrash_lib \
    -Wl,-rpath,. \
    -lpthread -ldl \
    -o "app_libc_crash"
strip --strip-all "app_libc_crash"

CRASH_SIG="./crash-sig-gen"
APP_NULL="./app_null_ptr"
APP_LIBC="./app_libc_crash"

for bin in "${CRASH_SIG}" "${APP_NULL}" "${APP_LIBC}"; do
    [[ -x "${bin}" ]] && pass "Binary exists: $(basename "${bin}")" \
                       || fail "Binary missing: $(basename "${bin}")"
done

for bin in "${APP_NULL}" "${APP_LIBC}" "./libcrash_lib.so"; do
    if file "${bin}" | grep -q "stripped"; then
        pass "Stripped: $(basename "${bin}")"
    else
        fail "NOT stripped: $(basename "${bin}")"
    fi
done

# ─── Helper: run a crash app (with optional extra args) and return .dmp path ─
run_crash() {
    local exe="$1"
    shift
    local args=("$@")

    rm -f /tmp/*.dmp 2>/dev/null || true

    local output
    output=$( "${exe}" "${args[@]}" 2>/dev/null || true )

    local dump_path
    dump_path=$(echo "${output}" | grep -o '/tmp/[^ ]*\.dmp' | head -1)

    if [[ -z "${dump_path}" || ! -f "${dump_path}" ]]; then
        dump_path=$(ls -t /tmp/*.dmp 2>/dev/null | head -1 || true)
    fi

    echo "${dump_path}"
}

# ─── Helper: validate Crash DNA format ───────────────────────────────────────
SIGNATURE_REGEX='^[0-9A-Fa-f]+_[0-9]+_[^_]+_0x[0-9a-fA-F]+$'

validate_sig() {
    local sig="$1" label="$2"
    if echo "${sig}" | grep -qE "${SIGNATURE_REGEX}"; then
        pass "${label}: ${sig}"
    else
        fail "${label}: invalid format '${sig}'"
    fi
}

# ─── Step 2: Null-pointer crash (SIGSEGV, 3 threads) ─────────────────────────
banner "Step 2: Null-Pointer Crash (SIGSEGV=11, 3 threads)"

info "Running app_null_ptr…"
DMP_NULL="$(run_crash "${APP_NULL}")"
if [[ -f "${DMP_NULL}" ]]; then
    pass "Dump file created: ${DMP_NULL}"
    mkdir -p "${MINIDUMP_DIR}"
    cp -f "${DMP_NULL}" "${MINIDUMP_DIR}/null_ptr_1.dmp"
else
    fail "No dump file produced by app_null_ptr"
    DMP_NULL=""
fi

SIG_NULL=""
if [[ -n "${DMP_NULL}" ]]; then
    SIG_NULL=$( "${CRASH_SIG}" "${DMP_NULL}" 2>&1 || true )
    validate_sig "${SIG_NULL}" "Null-ptr Crash DNA"
fi

# ─── Step 3: libc crash — double-free in crash_lib, no stack buffer ──────────
banner "Step 3: libc double-free Crash (SIGABRT=6, no stack buf)"

info "Running app_libc_crash…"
DMP_LIBC="$(run_crash "${APP_LIBC}")"
if [[ -f "${DMP_LIBC}" ]]; then
    pass "Dump file created: ${DMP_LIBC}"
    mkdir -p "${MINIDUMP_DIR}"
    cp -f "${DMP_LIBC}" "${MINIDUMP_DIR}/libc_crash_1.dmp"
else
    fail "No dump file produced by app_libc_crash"
    DMP_LIBC=""
fi

SIG_LIBC=""
if [[ -n "${DMP_LIBC}" ]]; then
    SIG_LIBC=$( "${CRASH_SIG}" "${DMP_LIBC}" 2>&1 || true )
    validate_sig "${SIG_LIBC}" "libc-crash (no stack) Crash DNA"
fi

# ─── Step 4: libc crash — double-free in crash_lib, with 2K stack buffer ─────
banner "Step 4: libc double-free Crash (SIGABRT=6, 2K stack buf)"

info "Running app_libc_crash stack…"
DMP_LIBC_STACK="$(run_crash "${APP_LIBC}" stack)"
if [[ -f "${DMP_LIBC_STACK}" ]]; then
    pass "Dump file created: ${DMP_LIBC_STACK}"
    mkdir -p "${MINIDUMP_DIR}"
    cp -f "${DMP_LIBC_STACK}" "${MINIDUMP_DIR}/libc_crash_stack.dmp"
else
    fail "No dump file produced by app_libc_crash stack"
    DMP_LIBC_STACK=""
fi

SIG_LIBC_STACK=""
if [[ -n "${DMP_LIBC_STACK}" ]]; then
    SIG_LIBC_STACK=$( "${CRASH_SIG}" "${DMP_LIBC_STACK}" 2>&1 || true )
    validate_sig "${SIG_LIBC_STACK}" "libc-crash (stack) Crash DNA"
fi

# ─── Step 5: ASLR-stability proof ────────────────────────────────────────────
banner "Step 5: ASLR Stability — run app_null_ptr a second time"

info "Second run of app_null_ptr…"
DMP_NULL2="$(run_crash "${APP_NULL}")"
SIG_NULL2=""
if [[ -f "${DMP_NULL2}" ]]; then
    mkdir -p "${MINIDUMP_DIR}"
    cp -f "${DMP_NULL2}" "${MINIDUMP_DIR}/null_ptr_2.dmp"
    SIG_NULL2=$( "${CRASH_SIG}" "${DMP_NULL2}" 2>&1 || true )
fi

extract_offset() { echo "$1" | cut -d_ -f4; }

if [[ -n "${SIG_NULL}" && -n "${SIG_NULL2}" ]]; then
    OFF1="$(extract_offset "${SIG_NULL}")"
    OFF2="$(extract_offset "${SIG_NULL2}")"
    if [[ "${OFF1}" == "${OFF2}" ]]; then
        pass "ASLR-stable offset: run1=${OFF1}  run2=${OFF2}"
    else
        fail "Offset changed between runs! run1=${OFF1}  run2=${OFF2}"
    fi
else
    info "Skipping ASLR check (missing signatures from one or both runs)"
fi

# ─── Summary ─────────────────────────────────────────────────────────────────
banner "Summary"

echo
echo -e "  Null-ptr Crash DNA        : ${BOLD}${SIG_NULL:-<N/A>}${RESET}"
echo -e "  libc-crash (no stack) DNA : ${BOLD}${SIG_LIBC:-<N/A>}${RESET}"
echo -e "  libc-crash (stack)    DNA : ${BOLD}${SIG_LIBC_STACK:-<N/A>}${RESET}"
echo

if [[ "${FAILURES}" -eq 0 ]]; then
    echo -e "${GREEN}${BOLD}ALL CHECKS PASSED${RESET}"
    exit 0
else
    echo -e "${RED}${BOLD}${FAILURES} CHECK(S) FAILED${RESET}"
    exit 1
fi
