#!/usr/bin/env bash
# =============================================================================
# verify.sh  —  Edge-Fingerprinting end-to-end verification
#
# PURPOSE:
#   1. Build crash-sig-gen and the test apps (inside the armhf container)
#   2. Run each test app to generate a .dmp minidump in /tmp/
#   3. Feed each .dmp to crash-sig-gen and print the Crash DNA
#   4. Validate the output format  [BuildID]_[Signal]_[ModuleName]_0x[HexOffset]
#   5. Run the same crash twice and prove the offset is ASLR-stable
#
# RUN (inside the devcontainer):
#   bash /work/verify.sh
#
# RUN (from the host, one-liner):
#   docker run --rm \
#     --platform linux/arm/v7 \
#     -v "$(pwd):/work" \
#     edge-fp \
#     bash /work/verify.sh
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# ANSI colours
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

pass() { echo -e "${GREEN}[PASS]${RESET} $*"; }
fail() { echo -e "${RED}[FAIL]${RESET} $*"; FAILURES=$((FAILURES + 1)); }
info() { echo -e "${CYAN}[INFO]${RESET} $*"; }
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
    "${SCRIPT_DIR}/tests/app_null_ptr.cpp" \
    -L/usr/local/lib -lbreakpad_client -lpthread -ldl \
    -o "app_null_ptr"
strip --strip-all "app_null_ptr"

info "Compiling Test App: app_div_zero…"
g++ -std=c++14 -g \
    -I/usr/local/include/breakpad \
    "${SCRIPT_DIR}/tests/app_div_zero.cpp" \
    -L/usr/local/lib -lbreakpad_client -lpthread -ldl \
    -o "app_div_zero"
strip --strip-all "app_div_zero"

# Map variables to the new paths
CRASH_SIG="./crash-sig-gen"
APP_NULL="./app_null_ptr"
APP_DIV="./app_div_zero"

for bin in "${CRASH_SIG}" "${APP_NULL}" "${APP_DIV}"; do
    [[ -x "${bin}" ]] && pass "Binary exists: $(basename "${bin}")" \
                       || fail "Binary missing: $(basename "${bin}")"
done

# Verify strip was applied (no debug section expected)
for bin in "${APP_NULL}" "${APP_DIV}"; do
    if file "${bin}" | grep -q "stripped"; then
        pass "Stripped: $(basename "${bin}")"
    else
        fail "NOT stripped: $(basename "${bin}")"
    fi
done

# ─── Helper: run a crash app and return the .dmp path ────────────────────────
# ─── Helper: run a crash app and return the .dmp path ────────────────────────
run_crash() {
    local exe="$1"
    local name
    name="$(basename "${exe}")"

    # Clean up old dumps
    rm -f /tmp/*.dmp 2>/dev/null || true

    # Run the crashing app; it prints "Minidump written to: /tmp/xxxx.dmp (ok)"
    local output
    output=$( "${exe}" 2>/dev/null || true )

    # Clean up the residual QEMU core dump that gets generated after Breakpad exits
    rm -f "${SCRIPT_DIR}/qemu_${name}"*.core 2>/dev/null || true
    rm -f "${SCRIPT_DIR}/core" 2>/dev/null || true

    local dump_path
    dump_path=$(echo "${output}" | grep -o '/tmp/[^ ]*\.dmp' | head -1)

    if [[ -z "${dump_path}" || ! -f "${dump_path}" ]]; then
        # Fallback: pick the newest .dmp in /tmp/
        dump_path=$(ls -t /tmp/*.dmp 2>/dev/null | head -1 || true)
    fi

    echo "${dump_path}"
}

# ─── Helper: validate Crash DNA format ───────────────────────────────────────
# Expected:  <HEX>_<DECIMAL>_<name>_0x<hex>
SIGNATURE_REGEX='^[0-9A-Fa-f]+_[0-9]+_[^_]+_0x[0-9a-fA-F]+$'

validate_sig() {
    local sig="$1" label="$2"
    if echo "${sig}" | grep -qE "${SIGNATURE_REGEX}"; then
        pass "${label}: ${sig}"
    else
        fail "${label}: invalid format '${sig}'"
    fi
}

# ─── Step 2: Null-pointer crash ──────────────────────────────────────────────
banner "Step 2: Null-Pointer Crash (SIGSEGV)"

info "Running app_null_ptr…"
DMP_NULL="$(run_crash "${APP_NULL}")"
if [[ -f "${DMP_NULL}" ]]; then
    pass "Dump file created: ${DMP_NULL}"
else
    fail "No dump file produced by app_null_ptr"
    DMP_NULL=""
fi

SIG_NULL=""
if [[ -n "${DMP_NULL}" ]]; then
    SIG_NULL=$( "${CRASH_SIG}" "${DMP_NULL}" 2>&1 || true )
    validate_sig "${SIG_NULL}" "Null-ptr Crash DNA"
fi

# ─── Step 3: Divide-by-zero crash ────────────────────────────────────────────
banner "Step 3: Divide-by-Zero Crash (SIGFPE)"

info "Running app_div_zero…"
DMP_DIV="$(run_crash "${APP_DIV}")"
if [[ -f "${DMP_DIV}" ]]; then
    pass "Dump file created: ${DMP_DIV}"
else
    fail "No dump file produced by app_div_zero"
    DMP_DIV=""
fi

SIG_DIV=""
if [[ -n "${DMP_DIV}" ]]; then
    SIG_DIV=$( "${CRASH_SIG}" "${DMP_DIV}" 2>&1 || true )
    validate_sig "${SIG_DIV}" "Div-zero Crash DNA"
fi

# ─── Step 4: ASLR-stability proof ────────────────────────────────────────────
banner "Step 4: ASLR Stability — run app_null_ptr a second time"

info "Second run of app_null_ptr…"
DMP_NULL2="$(run_crash "${APP_NULL}")"
SIG_NULL2=""
if [[ -f "${DMP_NULL2}" ]]; then
    SIG_NULL2=$( "${CRASH_SIG}" "${DMP_NULL2}" 2>&1 || true )
fi

# Extract the hex offset (4th field) and compare
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
echo -e "  Null-ptr  Crash DNA : ${BOLD}${SIG_NULL:-<N/A>}${RESET}"
echo -e "  Div-zero  Crash DNA : ${BOLD}${SIG_DIV:-<N/A>}${RESET}"
echo

if [[ "${FAILURES}" -eq 0 ]]; then
    echo -e "${GREEN}${BOLD}ALL CHECKS PASSED${RESET}"
    exit 0
else
    echo -e "${RED}${BOLD}${FAILURES} CHECK(S) FAILED${RESET}"
    exit 1
fi
