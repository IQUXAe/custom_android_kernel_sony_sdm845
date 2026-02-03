#!/bin/bash

set -e

# Add local toolchain to PATH
export PATH=$(pwd)/bin:$PATH
# Add local toolchain libraries to LD_LIBRARY_PATH to fix missing libxml2.so.2 and others
export LD_LIBRARY_PATH=$(pwd)/lib:$(pwd)/lib64:$LD_LIBRARY_PATH

export ARCH=arm64
export SUBARCH=arm64
export DEFCONFIG=tama_akari_defconfig

# Check if ld.lld exists to verify toolchain is present
if ! command -v ld.lld &> /dev/null; then
    echo "Error: ld.lld (LLVM linker) not found in $(pwd)/bin. Please run antman to download the toolchain."
    exit 1
fi

echo "Configuring kernel for $DEFCONFIG..."
make O=out $DEFCONFIG

echo "Starting build with Neutron Clang (LLVM)..."
make O=out \
    LLVM=1 \
    LLVM_IAS=1 \
    CROSS_COMPILE=aarch64-linux-gnu- \
    CROSS_COMPILE_ARM32=arm-linux-gnueabi- \
    CLANG_TRIPLE=aarch64-linux-gnu- \
    -j$(nproc --all) 2> >(grep -v "no version information available" >&2)

echo "Build compilation finished."
