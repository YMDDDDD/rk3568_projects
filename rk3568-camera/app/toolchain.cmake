# ============================================================================
# CMake 交叉编译工具链文件
# ============================================================================
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   "$ENV{CROSS_COMPILE}gcc")
set(CMAKE_CXX_COMPILER "$ENV{CROSS_COMPILE}g++")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_SYSROOT "$ENV{SYSROOT}")
set(CMAKE_C_FLAGS_INIT "--sysroot=$ENV{SYSROOT}")
set(CMAKE_CXX_FLAGS_INIT "--sysroot=$ENV{SYSROOT}")
