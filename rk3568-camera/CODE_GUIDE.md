# RK3568 摄像头项目源码指南

## 项目结构

```
rk3568-camera/
├── app/                        # ★ 核心源码
│   ├── CMakeLists.txt          # CMake 交叉编译配置
│   ├── toolchain.cmake         # 工具链文件
│   ├── main.cpp                # ★ 入口：所有模块创建、连接、启动
│   │
│   │── 基础层 ──────────────────────────────────────────
│   ├── config.h                # 编译期常量（分辨率/端口/路径/超时）
│   ├── frame_ref.h             # ★ 统一帧描述结构（全系统数据载体）
│   ├── pts_clock.h             # 全系统统一时间戳 CLOCK_MONOTONIC
│   ├── spsc_queue.h            # 无锁单产单消队列模板（displayQueue/encodeQueue）
│   │
│   │── 数据入口 ──────────────────────────────────────────
│   ├── buffer_pool.h/cpp       # ★ V4L2 mmap + EXPBUF + shared_ptr deleter 自动归还
│   └── capture.h/cpp           # ★ 采集线程：DQBUF→acquire→push 到 displayQueue+encodeQueue
│   │
│   │── 消费端 ──────────────────────────────────────────
│   ├── video_widget.h/cpp      # ★ OpenGL 渲染：Y+U+V 三个 GL_RED 纹理→NV12→RGB shader→屏幕
│   ├── mpp_encoder.h/cpp       # ★ MPP H.264 硬编码：memcpy→mpp_frame→encode_put_frame→NAL回调
│   ├── rtsp_server.h/cpp       # ★ 自实现 RTSP/RTP 服务器（TCP interleaved）
│   ├── segment_recorder.h/cpp  # 分段录像：NAL→.h264 文件，5分钟轮转
│   └── detector.h/cpp          # YOLO 推理线程：原生 V4L2 从 video1 读→RKNN NPU
│   │
│   │── 辅助 ──────────────────────────────────────────
│   ├── mainwindow.h/cpp        # Qt 主窗口：工具栏/性能面板/录像列表
│   ├── watchdog.h/cpp          # 心跳监控+超时恢复
│   └── perf_monitor.h/cpp      # FPS/延迟/队列深度统计
│
├── third_party/spdlog/         # spdlog v1.12.0（header-only 日志库）
├── model/                      # YOLO 模型文件（coco_labels.txt）
├── tests/                      # ★ 测试程序
│   ├── test_v4l2_info.c        # V4L2 设备全面查询工具
│   ├── test_v4l2_8001280.c     # 800×1280 格式+stride 测试
│   ├── test_video0_video1.c    # ISP 双路同时采集验证
│   ├── test_dmabuf_mpp.c       # DMA-BUF→MPP 零拷贝验证
│   └── test_mpp_real.c         # MPP 编码器最小验证
├── scripts/                    # build/deploy/convert_model 脚本
├── buildroot/configs/          # Buildroot defconfig 片段
└── WORKLOG.md                  # ★ 工作日志（完整开发记录）
```

---

## 源码阅读顺序

### 第一层：数据结构（理解"血液"怎么流）

| 顺序 | 文件 | 看什么 | 行数 |
|:--:|------|------|:--:|
| 1 | `config.h` | 所有编译期常量 | ~58 |
| 2 | `frame_ref.h` | **核心数据结构**——每帧数据的统一描述，全系统用它传递帧 | ~20 |
| 3 | `pts_clock.h` | 全系统统一时间戳 CLOCK_MONOTONIC | ~20 |
| 4 | `spsc_queue.h` | 无锁队列——displayQueue 和 encodeQueue 都是它 | ~90 |

### 第二层：数据入口（理解数据怎么来的）

| 顺序 | 文件 | 看什么 | 行数 |
|:--:|------|------|:--:|
| 5 | `buffer_pool.h/cpp` | V4L2 mmap + EXPBUF + **shared_ptr 自定义 deleter 自动归还** | ~170 |
| 6 | `capture.h/cpp` | 采集线程 QTimer 驱动：DQBUF → acquire → push 双队列 | ~110 |

### 第三层：系统拼装（理解模块怎么协作）

| 顺序 | 文件 | 看什么 | 行数 |
|:--:|------|------|:--:|
| 7 | **`main.cpp`** | **最重要**——所有模块创建、初始化、信号连接、启动顺序、清理 | ~120 |

### 第四层：消费端（理解数据怎么用的）

| 顺序 | 文件 | 看什么 | 行数 |
|:--:|------|------|:--:|
| 8 | `video_widget.h/cpp` | OpenGL NV12 渲染（三个 GL_RED 纹理 + shader） | ~200 |
| 9 | `mpp_encoder.h/cpp` | MPP 硬编码：memcpy→mpp_frame→encode_put_frame→NAL 回调 | ~160 |
| 10 | `rtsp_server.h/cpp` | 自实现 RTSP/RTP 服务器（TCP interleaved 推流） | ~290 |
| 11 | `segment_recorder.h/cpp` | 分段录像：直接写 .h264 裸流，5 分钟一段 | ~120 |
| 12 | `detector.h/cpp` | YOLO 推理：原生 V4L2 读 video1→RKNN NPU 推理 | ~190 |

### 第五层：界面与辅助

| 顺序 | 文件 | 看什么 |
|:--:|------|------|
| 13 | `mainwindow.h/cpp` | Qt 工具栏 + 性能面板 + 录像列表 |
| 14 | `watchdog.h/cpp` | 心跳监控 + 超时恢复 |
| 15 | `perf_monitor.h/cpp` | FPS/延迟/队列深度统计 |

---

## 数据流转全景

```
                        ISP (rkisp-vir0)
                       ┌───────┴───────┐
                  mainpath          selfpath
               /dev/video0         /dev/video1
              1920×1080 NV12      640×640 NV12
                   │                   │
                   ▼                   ▼
           CaptureThread          DetectThread
               tick()               run()
           DQBUF→acquire          DQBUF→preprocess
                   │                   │
        ┌──────────┼──────────┐      rknn_run()→NPU
        ▼          ▼          ▼         │
  displayQueue  encodeQueue (detectQueue) →  detectionReady信号
    (深度1)      (深度4)                   │
        │          │                      ▼
        ▼          ▼              MainWindow::onDetection
  main.cpp     mpp_encoder              │
  displayTimer   tick()                 ▼
    10ms轮询      pop→encodeOneFrame  VideoWidget
        │          │                 setDetections
        ▼          ▼                      │
  renderRawNV12  encode_put_frame         ▼
  Y+U+V纹理上传  encode_get_packet    paintGL
  NV12→RGB      NAL callback        QPainter画框
  Shader            │
        │      ┌────┴────┐
        ▼      ▼         ▼
     Wayland  RTSP推流  分段录像
     屏幕    VLC播放   /userdata/records/*.h264
```

---

## 关键设计决策

### 1. 为什么用 shared_ptr deleter 而不是手动 release？

```
旧方案（有问题）:
  acquire→refCount=1 → display release → 归零 QBUF
                    → encoder release → 重复 QBUF ❌

新方案（当前）:
  acquire→shared_ptr(deleter) → display 析构 → use_count-1
                              → encoder 析构 → use_count-1→0→deleter→QBUF ✅
```

### 2. 为什么全部用主线程 QTimer 驱动？

RK ISP 驱动不支持跨线程 V4L2 ioctl。所有 V4L2 操作（DQBUF/QBUF）必须在同一线程。

### 3. 为什么 RTSP 自己实现而非用 live555/MediaMTX？

- live555：API 老旧，下载失败
- MediaMTX：ARM 二进制被 GitHub 限速截断
- 自实现：QTcpServer + RTP 打包，约 300 行代码，零外部依赖

### 4. 为什么 stride 曾经是 2112？

`G_FMT` 读回旧 bytesperline → 没清零 → `S_FMT` 时驱动保留 2112。修复：`bytesperline=0` 让驱动自算。

### 5. 为什么渲染色彩曾偏绿？

Mali GPU 不支持 `GL_RG` 格式（OpenGL ES 2.0 核心无此格式）。改为 CPU 分离 U/V 到两个 `GL_RED` 纹理。

---

## 性能指标

| 指标 | 数值 |
|------|------|
| 采集帧率 | 30fps |
| 显示帧率 | ~29fps（290帧/10秒） |
| 编码帧率 | 30fps |
| 渲染耗时 | ~5ms/帧 |
| RTSP 延时 | ~3s（VLC 缓冲为主） |
| 编码码率 | 4Mbps CBR |
| V4L2 buffer | 8个 × 3.3MB |

---

## 数据布景

```
NV12 V4L2 buffer 布局（1920×1080）:
┌─────────────────────┐ offset 0
│ Y 平面              │
│ 1920字节×1080行      │ size = 1920×1080 = 2073600
│ stride=1920（紧密）   │
├─────────────────────┤ offset 2073600
│ UV 平面             │
│ U V U V...交错       │ size = 1920×540 = 1036800
│ 紧密排列，无 padding  │
└─────────────────────┘ offset 3110400 (total)
```
