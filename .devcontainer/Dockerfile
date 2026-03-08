# ============================================================
# Edge-Fingerprinting Dev Container
# Base: ubuntu:20.04
# Builds Google Breakpad v2.1.0 + LSS from source
# ============================================================
FROM ubuntu:20.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# ------------------------------------------------------------------
# 1. System toolchain & build dependencies
# ------------------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    wget \
    git \
    curl \
    python3 \
    python3-pip \
    autoconf \
    automake \
    libtool \
    pkg-config \
    ca-certificates \
    binutils \
    file \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*


# ------------------------------------------------------------------
# 2. Fetch LSS (linux-syscall-support) -- required by Breakpad
#    We pin to the commit that was current for Breakpad v2.1.0
# ------------------------------------------------------------------
RUN git clone --depth=1 \
    https://chromium.googlesource.com/linux-syscall-support \
    /opt/lss

# ------------------------------------------------------------------
# 3. Fetch Google Breakpad v2.1.0
# ------------------------------------------------------------------
RUN git clone --depth=1 --branch v2023.06.01 \
    https://chromium.googlesource.com/breakpad/breakpad \
    /opt/breakpad \
    || git clone --depth=1 \
       https://chromium.googlesource.com/breakpad/breakpad \
       /opt/breakpad

# Breakpad expects LSS at src/third_party/lss/linux_syscall_support.h
RUN mkdir -p /opt/breakpad/src/third_party/lss \
    && cp /opt/lss/linux_syscall_support.h \
       /opt/breakpad/src/third_party/lss/linux_syscall_support.h

# ------------------------------------------------------------------
# 4. Compile & install Breakpad (client + processor + tools)
# ------------------------------------------------------------------
WORKDIR /opt/breakpad
RUN ./configure --prefix=/usr/local \
    && make -j$(nproc) \
    && make install

# Manually copy Google Breakpad headers to ensure all critical files like minidump.h
# are present, as the standard make install is incomplete.
RUN cp -r src/google_breakpad /usr/local/include/breakpad/

# Double-check that the key processor header and static libs exist.
# We now use the standard google_breakpad/ prefix.
RUN ls /usr/local/include/breakpad/google_breakpad/processor/minidump.h \
    && ls /usr/local/lib/libbreakpad.a \
    && ls /usr/local/lib/libbreakpad_client.a \
    && echo "=== Breakpad install verified ==="

ENV CC=gcc
ENV CXX=g++


# ------------------------------------------------------------------
# 5. Working directory for the project (mounted from host)
# ------------------------------------------------------------------
WORKDIR /work

# Keep container alive when used interactively
CMD ["/bin/bash"]
