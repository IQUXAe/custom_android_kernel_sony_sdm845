#!/bin/bash

set -e

export ARCH=arm64
export SUBARCH=arm64
export DEFCONFIG=tama_akari_defconfig

export CC=clang

if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
    echo "Error: aarch64-linux-gnu-gcc not found."
    exit 1
fi

if ! command -v arm-linux-gnueabi-gcc &> /dev/null; then
    echo "Error: arm-linux-gnueabi-gcc not found."
    exit 1
fi

export CROSS_COMPILE=aarch64-linux-gnu-
export CROSS_COMPILE_ARM32=arm-linux-gnueabi-
export CLANG_TRIPLE=aarch64-linux-gnu-

export CLANG_TARGET_ARM32="--target=arm-linux-gnueabi"
export CLANG_GCC32_TC="--gcc-toolchain=/usr"

echo "Configuring kernel for $DEFCONFIG..."
make O=out $DEFCONFIG

echo "Starting build with Clang..."
make O=out \
    CC=clang \
    CROSS_COMPILE=aarch64-linux-gnu- \
    CROSS_COMPILE_ARM32=arm-linux-gnueabi- \
    CLANG_TRIPLE=aarch64-linux-gnu- \
    CLANG_TARGET_ARM32="--target=arm-linux-gnueabi" \
    CLANG_GCC32_TC="--gcc-toolchain=/usr" \
    -j$(nproc --all)

echo "Build compilation finished."
