#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${OUT_DIR:-out}"
ARCH="${ARCH:-arm64}"
SUBARCH="${SUBARCH:-arm64}"
DEVICE="${DEVICE:-akari}"
DEFCONFIG="${DEFCONFIG:-tama_akari_defconfig}"
JOBS="${JOBS:-4}"
CLANG_MODE="${CLANG_MODE:-system}"
CLANG_ARCHIVE_URL="${CLANG_ARCHIVE_URL:-}"
CLANG_DIR="${CLANG_DIR:-}"
MAKE_TARGETS="${MAKE_TARGETS:-}"
EXTRA_MAKE_ARGS="${EXTRA_MAKE_ARGS:-}"

export ARCH
export SUBARCH

log() {
    printf '[ci-build] %s\n' "$*"
}

die() {
    printf '[ci-build] ERROR: %s\n' "$*" >&2
    exit 1
}

resolve_jobs() {
    local max_jobs

    if [[ "${JOBS}" == "auto" ]]; then
        max_jobs="$(nproc --all)"
        if (( max_jobs > 4 )); then
            JOBS=4
        else
            JOBS="${max_jobs}"
        fi
    fi

    [[ "${JOBS}" =~ ^[0-9]+$ ]] || die "JOBS must be a positive integer or 'auto'"
    (( JOBS > 0 )) || die "JOBS must be greater than zero"
}

extract_archive() {
    local archive="$1"
    local destination="$2"

    mkdir -p "${destination}"

    case "${archive}" in
        *.tar.gz|*.tgz)
            tar -xzf "${archive}" -C "${destination}"
            ;;
        *.tar.xz|*.txz)
            tar -xJf "${archive}" -C "${destination}"
            ;;
        *.tar.zst)
            tar -I zstd -xf "${archive}" -C "${destination}"
            ;;
        *.tar)
            tar -xf "${archive}" -C "${destination}"
            ;;
        *.zip)
            unzip -q "${archive}" -d "${destination}"
            ;;
        *)
            die "Unsupported clang archive format: ${archive}"
            ;;
    esac
}

find_toolchain_root() {
    local search_root="$1"
    local candidate

    while IFS= read -r candidate; do
        if [[ -x "${candidate}/bin/clang" && -x "${candidate}/bin/ld.lld" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done < <(find "${search_root}" -mindepth 0 -maxdepth 3 -type d | sort)

    return 1
}

setup_archive_toolchain() {
    local archive_path
    local archive_name
    local unpack_dir
    local detected_root

    [[ -n "${CLANG_ARCHIVE_URL}" ]] || \
        die "CLANG_ARCHIVE_URL is required when CLANG_MODE=archive"

    archive_name="$(basename "${CLANG_ARCHIVE_URL%%\?*}")"
    [[ -n "${archive_name}" && "${archive_name}" != "/" ]] || archive_name="clang-toolchain.tar.gz"

    archive_path="${RUNNER_TEMP:-/tmp}/${archive_name}"
    unpack_dir="${RUNNER_TEMP:-/tmp}/clang-toolchain"

    rm -rf "${archive_path}" "${unpack_dir}"

    log "Downloading clang archive from ${CLANG_ARCHIVE_URL}"
    curl --fail --location --retry 3 --output "${archive_path}" "${CLANG_ARCHIVE_URL}"
    extract_archive "${archive_path}" "${unpack_dir}"

    detected_root="$(find_toolchain_root "${unpack_dir}")" || \
        die "Could not locate clang toolchain root in extracted archive"

    CLANG_DIR="${detected_root}"
}

setup_local_toolchain() {
    [[ -n "${CLANG_DIR}" ]] || CLANG_DIR="${ROOT_DIR}"

    [[ -x "${CLANG_DIR}/bin/clang" ]] || die "clang not found in ${CLANG_DIR}/bin"
    [[ -x "${CLANG_DIR}/bin/ld.lld" ]] || die "ld.lld not found in ${CLANG_DIR}/bin"
}

setup_system_toolchain() {
    command -v clang >/dev/null 2>&1 || die "system clang not found in PATH"
    command -v ld.lld >/dev/null 2>&1 || die "system ld.lld not found in PATH"
}

setup_toolchain() {
    case "${CLANG_MODE}" in
        system)
            setup_system_toolchain
            ;;
        local)
            setup_local_toolchain
            ;;
        archive)
            setup_archive_toolchain
            ;;
        *)
            die "Unsupported CLANG_MODE=${CLANG_MODE}. Expected system, local, or archive"
            ;;
    esac

    if [[ -n "${CLANG_DIR}" ]]; then
        export PATH="${CLANG_DIR}/bin:${PATH}"

        if [[ -d "${CLANG_DIR}/lib" || -d "${CLANG_DIR}/lib64" ]]; then
            export LD_LIBRARY_PATH="${CLANG_DIR}/lib:${CLANG_DIR}/lib64:${LD_LIBRARY_PATH:-}"
        fi
    fi
}

show_tool_versions() {
    log "Using device=${DEVICE} defconfig=${DEFCONFIG} jobs=${JOBS} clang_mode=${CLANG_MODE}"
    log "clang: $(command -v clang)"
    clang --version | sed -n '1,2p'
    log "ld.lld: $(command -v ld.lld)"
}

build_kernel() {
    local make_args=()

    make_args+=("O=${OUT_DIR}")
    make_args+=("LLVM=1" "LLVM_IAS=1")
    make_args+=("CROSS_COMPILE=aarch64-linux-gnu-")
    make_args+=("CROSS_COMPILE_ARM32=arm-linux-gnueabi-")
    make_args+=("CLANG_TRIPLE=aarch64-linux-gnu-")

    if [[ -n "${EXTRA_MAKE_ARGS}" ]]; then
        # Intentional word splitting for user-supplied make arguments.
        # shellcheck disable=SC2206
        make_args+=(${EXTRA_MAKE_ARGS})
    fi

    log "Running defconfig ${DEFCONFIG}"
    make "${make_args[@]}" "${DEFCONFIG}"

    log "Building kernel"
    if [[ -n "${MAKE_TARGETS}" ]]; then
        # shellcheck disable=SC2206
        local target_args=(${MAKE_TARGETS})
        make "${make_args[@]}" -j"${JOBS}" "${target_args[@]}"
    else
        make "${make_args[@]}" -j"${JOBS}"
    fi
}

collect_artifacts() {
    local artifact_dir="${ROOT_DIR}/artifacts"

    rm -rf "${artifact_dir}"
    mkdir -p "${artifact_dir}"

    cp "${ROOT_DIR}/${OUT_DIR}/.config" "${artifact_dir}/kernel-${DEVICE}.config"

    if [[ -f "${ROOT_DIR}/${OUT_DIR}/arch/arm64/boot/Image" ]]; then
        cp "${ROOT_DIR}/${OUT_DIR}/arch/arm64/boot/Image" "${artifact_dir}/Image-${DEVICE}"
    fi

    if [[ -f "${ROOT_DIR}/${OUT_DIR}/arch/arm64/boot/Image.gz-dtb" ]]; then
        cp "${ROOT_DIR}/${OUT_DIR}/arch/arm64/boot/Image.gz-dtb" "${artifact_dir}/Image.gz-dtb-${DEVICE}"
    fi

    if [[ -f "${ROOT_DIR}/${OUT_DIR}/vmlinux" ]]; then
        cp "${ROOT_DIR}/${OUT_DIR}/vmlinux" "${artifact_dir}/vmlinux-${DEVICE}"
    fi
}

main() {
    cd "${ROOT_DIR}"
    resolve_jobs
    setup_toolchain
    show_tool_versions
    build_kernel
    collect_artifacts
}

main "$@"
