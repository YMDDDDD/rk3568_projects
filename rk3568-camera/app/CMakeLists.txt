cmake_minimum_required(VERSION 3.16)
project(rk3568-camera VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# 交叉编译检测
# ============================================================================
if(DEFINED ENV{CROSS_COMPILE})
    set(CMAKE_C_COMPILER   "$ENV{CROSS_COMPILE}gcc")
    set(CMAKE_CXX_COMPILER "$ENV{CROSS_COMPILE}g++")
endif()

if(DEFINED ENV{SYSROOT})
    set(CMAKE_SYSROOT "$ENV{SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()

# ============================================================================
# Qt5
# ============================================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL Network)

# ============================================================================
# 依赖库
# ============================================================================

# FFmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale)

# MPP (Rockchip Media Process Platform)
pkg_check_modules(MPP REQUIRED rockchip_mpp)

# RKNN (Rockchip NPU runtime)
find_path(RKNN_INCLUDE_DIR rknn_api.h
    HINTS /usr/include /usr/local/include
    PATH_SUFFIXES rknn)
find_library(RKNN_LIBRARY rknnrt
    HINTS /usr/lib /usr/local/lib)

if(NOT RKNN_INCLUDE_DIR OR NOT RKNN_LIBRARY)
    message(WARNING "RKNN not found, YOLO detection will be disabled")
    add_definitions(-DDISABLE_RKNN)
    set(RKNN_INCLUDE_DIR "")   # 防止空路径导致 CMake 错误
    set(RKNN_LIBRARY "")
endif()

# spdlog（header-only，本地自带）
set(SPDLOG_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../third_party/spdlog")
if(NOT EXISTS "${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h")
    message(FATAL_ERROR "spdlog headers not found at ${SPDLOG_INCLUDE_DIR}")
endif()
message(STATUS "Using spdlog from: ${SPDLOG_INCLUDE_DIR}")

# EGL + libdrm (for dmabuf import)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(DRM REQUIRED libdrm)

# ============================================================================
# 源文件
# ============================================================================
set(SOURCES
    buffer_pool.cpp
    capture.cpp
    mpp_encoder.cpp
    rtsp_server.cpp
    segment_recorder.cpp
    detector.cpp
    video_widget.cpp
    watchdog.cpp
    perf_monitor.cpp
    mainwindow.cpp
    main.cpp
)

set(HEADERS
    config.h
    frame_ref.h
    pts_clock.h
    spsc_queue.h
    buffer_pool.h
    capture.h
    mpp_encoder.h
    rtsp_server.h
    segment_recorder.h
    detector.h
    video_widget.h
    watchdog.h
    perf_monitor.h
    mainwindow.h
)

# ============================================================================
# 可执行文件
# ============================================================================
add_executable(rk3568-camera ${SOURCES} ${HEADERS})

target_include_directories(rk3568-camera PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Qt5Widgets_INCLUDE_DIRS}
    ${Qt5OpenGL_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${MPP_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIR}
    ${EGL_INCLUDE_DIRS}
    ${DRM_INCLUDE_DIRS}
)
if(RKNN_INCLUDE_DIR)
    target_include_directories(rk3568-camera PRIVATE ${RKNN_INCLUDE_DIR})
endif()

target_link_libraries(rk3568-camera PRIVATE
    Qt5::Widgets
    Qt5::OpenGL
    Qt5::Network
    ${FFMPEG_LIBRARIES}
    ${MPP_LIBRARIES}
    ${EGL_LIBRARIES}
    ${DRM_LIBRARIES}
    pthread
)

if(RKNN_LIBRARY)
    target_include_directories(rk3568-camera PRIVATE ${RKNN_INCLUDE_DIR})
    target_link_libraries(rk3568-camera PRIVATE ${RKNN_LIBRARY})
endif()

# spdlog 为 header-only 库，无需链接额外 .so


# ============================================================================
# 安装
# ============================================================================
install(TARGETS rk3568-camera DESTINATION /usr/bin)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/../model/" DESTINATION /usr/share/rk3568-camera/model
    OPTIONAL FILES_MATCHING PATTERN "*.rknn" PATTERN "*.txt")

# ============================================================================
# 编译选项
# ============================================================================
target_compile_options(rk3568-camera PRIVATE -Wall -Wextra -O2)
