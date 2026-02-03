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

# KernelPatch (APatch) integration
KERNEL_IMAGE="out/arch/arm64/boot/Image"
KERNEL_GZ_DTB="out/arch/arm64/boot/Image.gz-dtb"
KERNEL_BACKUP="out/arch/arm64/boot/Image.orig"
PATCHED_TEMP="out/arch/arm64/boot/Image.patched"
DTB_FILE="out/arch/arm64/boot/dts/qcom/sdm845-tama-akari-rows.dtb"
KPTOOLS="./kpatch/kptools"
KPIMG="./kpatch/kpimg"

if [ -f "$KERNEL_IMAGE" ] && [ -f "$KPTOOLS" ] && [ -f "$KPIMG" ]; then
    echo ""
    echo "Patching kernel with KernelPatch (APatch)..."
    
    # Generate random superkey if not provided
    SUPERKEY="${SUPERKEY:-$(head -c 8 /dev/urandom | xxd -p)}"
    
    # Backup original kernel
    cp "$KERNEL_IMAGE" "$KERNEL_BACKUP"
    
    # Patch uncompressed Image
    $KPTOOLS -p -i "$KERNEL_IMAGE" -k "$KPIMG" -s "$SUPERKEY" -o "$PATCHED_TEMP"
    
    if [ -f "$PATCHED_TEMP" ]; then
        # Replace original with patched
        mv "$PATCHED_TEMP" "$KERNEL_IMAGE"
        
        # Rebuild Image.gz-dtb
        echo "Rebuilding Image.gz-dtb..."
        gzip -9 -c "$KERNEL_IMAGE" > "out/arch/arm64/boot/Image.gz"
        
        # Find and concatenate DTB files
        cat out/arch/arm64/boot/Image.gz out/arch/arm64/boot/dts/qcom/*.dtb > "$KERNEL_GZ_DTB"
        
        echo ""
        echo "=========================================="
        echo "KernelPatch applied successfully!"
        echo "Patched image: $KERNEL_GZ_DTB"
        echo "Original backup: $KERNEL_BACKUP"
        echo "SuperKey: $SUPERKEY"
        echo "=========================================="
        echo ""
        echo "Use this superkey in APatch Manager app."
    else
        echo "Warning: KernelPatch failed. Original image is still available."
    fi
else
    if [ ! -f "$KERNEL_IMAGE" ]; then
        echo "Warning: Kernel image not found at $KERNEL_IMAGE"
    fi
    if [ ! -f "$KPTOOLS" ] || [ ! -f "$KPIMG" ]; then
        echo "Note: KernelPatch tools not found. Skipping APatch integration."
        echo "To enable, download kptools and kpimg to ./kpatch/"
    fi
fi

