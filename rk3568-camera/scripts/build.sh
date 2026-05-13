#!/bin/bash
# ============================================================================
# 交叉编译脚本
# 用法: ./build.sh [debug|release]
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
APP_DIR="${PROJECT_DIR}/app"
BUILD_DIR="${APP_DIR}/build"

# --- 工具链配置 ---
SYSROOT="${SYSROOT:-/home/dxy/linux/rk3568_sdk/buildroot/output/rockchip_atk_dlrk3568/host/aarch64-buildroot-linux-gnu/sysroot}"
CROSS_TOOLCHAIN="${CROSS_TOOLCHAIN:-/home/dxy/linux/rk3568_sdk/buildroot/output/rockchip_atk_dlrk3568/host/bin/aarch64-buildroot-linux-gnu}"

export CROSS_COMPILE="${CROSS_TOOLCHAIN}-"
export SYSROOT="${SYSROOT}"
export PKG_CONFIG_PATH="${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"

BUILD_TYPE="${1:-release}"
case "${BUILD_TYPE}" in
    debug)   CMAKE_BUILD_TYPE="Debug" ;;
    release) CMAKE_BUILD_TYPE="Release" ;;
    *)       CMAKE_BUILD_TYPE="Release" ;;
esac

echo "============================================"
echo "Building rk3568-camera (${CMAKE_BUILD_TYPE})"
echo "  CROSS_COMPILE: ${CROSS_COMPILE}"
echo "  SYSROOT:       ${SYSROOT}"
echo "============================================"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${APP_DIR}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DCMAKE_TOOLCHAIN_FILE="${APP_DIR}/toolchain.cmake" 2>/dev/null || \
cmake "${APP_DIR}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"

make -j$(nproc)

echo ""
echo "Build success: ${BUILD_DIR}/rk3568-camera"
ls -lh "${BUILD_DIR}/rk3568-camera"
