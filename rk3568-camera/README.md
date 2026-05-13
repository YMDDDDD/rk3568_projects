# RK3568 智能摄像头

基于 RK3568 + OV13850 的智能摄像头系统，实现了：

- MIPI CSI 摄像头视频采集（OV13850）
- ISP 双路径硬件分流（video0 1080p + video1 640×640）
- V4L2 DMA-BUF 零拷贝 + BufferPool + 一入多出队列
- MPP 硬件 H.264 编码
- RTSP 网络推流（live555 内嵌）
- 分段 MP4 本地录制（5 分钟 / 段）
- Qt5 OpenGL 视频渲染（dmabuf → EGL 零拷贝上屏）
- YOLOv5s + RKNN NPU 目标检测
- Watchdog 异常监控 + 自恢复
- 统一 CLOCK_MONOTONIC 时间戳
- spdlog 日志 + PerfMonitor 性能监控

## 目录结构

```
rk3568-camera/
├── app/                 # 应用源码 (CMake)
├── third_party/live555/ # live555 源码
├── model/               # YOLOv5s ONNX/RKNN 模型
├── scripts/             # 编译/部署/模型转换脚本
├── buildroot/configs/   # Buildroot 配置片段
└── SOLUTION.md -> ../SOLUTION.md  # 详细方案设计文档
```

## 快速开始

### 1. 编译

```bash
# 设置交叉工具链路径
export CROSS_COMPILE=/path/to/aarch64-buildroot-linux-gnu-
export SYSROOT=/path/to/sysroot

cd app && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

或使用脚本：

```bash
./scripts/build.sh release
```

### 2. 部署

```bash
./scripts/deploy.sh
```

### 3. 运行

```bash
# 在开发板上
export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_INTEGRATION=eglfs_kms
/usr/bin/rk3568-camera
```

## RTSP 拉流

```
rtsp://<开发板IP>:8554/live
```

用 VLC 或 ffplay 播放。

## 模型转换

```bash
# 需要 rknn-toolkit2
./scripts/convert_model.sh
```

## 详细方案

见 [SOLUTION.md](../SOLUTION.md)
