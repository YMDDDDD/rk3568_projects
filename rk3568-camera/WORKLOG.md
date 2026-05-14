# 工作日志

## 2026-05-13

### 14:00 — 方案设计与验证阶段

**ISP 双路径验证（开发板）**
- 通过 `v4l2-ctl` 验证 `/dev/video0` (rkisp_mainpath) 可输出 1920×1080 NV12 ✅
- 通过 `v4l2-ctl` 验证 `/dev/video1` (rkisp_selfpath) 可输出 640×640 NV12 ✅
- 确认内核 `CONFIG_VIDEO_OV13850=y` 已启用 ✅
- 交叉编译工具链已就绪：`aarch64-buildroot-linux-gnu-gcc` ✅
- dmabuf 导出：`v4l2-ctl` 版本老不支持 `--export-buffer` 参数，内核 `VIDIOC_EXPBUF` 代码级可用

### 14:30 — 技术方案文档

- 写入 `SOLUTION.md` v1：FFmpeg + MPP + live555 + Qt + RKNN 完整方案
- 用户反馈选择纯 FFmpeg 路线（放弃 GStreamer）
- 用户确认 OV13850 + Buildroot + YOLOv5s + RTSP（不要 RTMP）
- 用户确认接受"MPP 原生编码 + FFmpeg 封装"混合方案

### 15:00 — 方案优化（suggest.md 评审）

审阅 `suggest.md` 中的 7 个问题和 5 个建议，采纳了 5 个关键修改：

| # | 问题 | 采纳状态 |
|---|------|---------|
| 1 | AVFrame 深拷贝 DDR 压力大 | ✅ 改为 DMA-BUF + BufferPool |
| 2 | 显示和编码抢同一个队列 | ✅ 改为一入多出 (DisplayQueue + EncodeQueue) |
| 3 | live555 → MediaMTX | ❌ 保留 live555 |
| 4 | 缺少统一时间戳 | ✅ 改为 CLOCK_MONOTONIC |
| 5 | 无 watchdog | ✅ 新增 Watchdog 模块 |
| 6 | YOLOv5s → YOLOv5n | ❌ 保留 YOLOv5s |
| 7 | 缺少录像分段 | ✅ 改为 5 分钟分段 MP4 |

额外采纳的配套改进：BufferPool、统一 FrameRef 结构、SystemState 状态机、spdlog 日志、PerfMonitor 性能监控。

- 更新 `SOLUTION.md` 为 v2 版本

### 15:30 — 项目骨架搭建

创建 `rk3568-camera/` 工程，共计 34 个文件：

**基础层（6 文件）：**
- `config.h` — 编译期配置常量
- `frame_ref.h` — 统一帧描述结构（dmabuf fd + 引用计数）
- `pts_clock.h` — 全系统统一时间戳 (CLOCK_MONOTONIC)
- `spsc_queue.h` — 无锁单产单消队列模板
- `buffer_pool.h/.cpp` — V4L2 DMA-BUF BufferPool（mmap + EXPBUF + 引用计数）

**业务模块（18 文件）：**
- `capture.h/.cpp` — V4L2 采集线程（mmap → BufferPool → DisplayQueue + EncodeQueue）
- `mpp_encoder.h/.cpp` — MPP 硬件编码线程（dmabuf fd 导入 MPP → H.264 NAL）
- `rtsp_server.h/.cpp` — live555 内嵌 RTSP 服务（**TODO**: live555 实际集成）
- `segment_recorder.h/.cpp` — FFmpeg 分段 MP4 录制（5min/段，保留 120 段）
- `detector.h/.cpp` — YOLO 推理线程（video1 独立读 640×640 → RKNN，**TODO**: YOLO 后处理）
- `video_widget.h/.cpp` — Qt OpenGL 渲染控件（**TODO**: EGL dmabuf import）
- `watchdog.h/.cpp` — 线程心跳监控 + 超时自动恢复
- `perf_monitor.h/.cpp` — 性能监控（FPS / 延迟 / 队列深度 / 温度）
- `mainwindow.h/.cpp` — Qt 主窗口（工具栏 + 状态栏 + 性能面板）
- `main.cpp` — 启动入口（日志初始化 + 模块创建 + 信号连接）

**构建系统（2 文件）：**
- `CMakeLists.txt` — CMake 交叉编译配置（Qt5 + FFmpeg + MPP + RKNN + DRM）
- `toolchain.cmake` — 交叉工具链文件

**脚本和配置（7 文件）：**
- `scripts/build.sh` — 一键交叉编译
- `scripts/deploy.sh` — 一键部署到开发板
- `scripts/convert_model.sh` — ONNX → RKNN 模型转换
- `buildroot/configs/rk3568_camera_defconfig` — Buildroot 配置片段
- `model/coco_labels.txt` — COCO 80 类别标签
- `README.md` — 项目说明
- `WORKLOG.md` — 本文件

### 16:00 — 待办事项记录

以下标记为 TODO 的项需要在后续完成：

1. `rtsp_server.cpp` — live555 实际集成（当前是占位）
2. `video_widget.cpp` — `EGL_EXT_image_dma_buf_import` 实现（当前 fallback 到 glTexSubImage2D）
3. `detector.cpp` — YOLO 后处理解码（NMS + 坐标映射 + 标签匹配）
4. `mainwindow.cpp` — 通过依赖注入关联 capture_、encoder_ 等模块指针（当前 main.cpp 中创建但未注入到 MainWindow）
5. `main.cpp` — 编码线程的启动逻辑需要等 MainWindow 按钮触发，当前仅创建了对象未启动

---

### 16:30 — 开发板依赖库确认

通过 ADB 检查 RK3568 开发板上的关键库：
- MPP: `/usr/lib/librockchip_mpp.so` ✅
- libdrm: `/usr/lib/libdrm.so` ✅
- EGL: `/usr/lib/libEGL.so` ✅
- GLESv2: `/usr/lib/libGLESv2.so` ✅
- RKNN: `/usr/lib/librknnrt.so` ✅
- RGA: `/usr/lib/librga.so` ✅
- FFmpeg: `/usr/lib/libavformat.so` ✅
- Qt5: `/usr/lib/libQt5*.so` (15.8) ✅

SDK sysroot 头文件确认：
- MPP headers: `/usr/include/rockchip/` (23个头文件) ✅
- RKNN headers: `/usr/include/rknn_api.h` ✅
- EGL dmabuf 扩展: `EGL_EXT_image_dma_buf_import` 已定义 ✅
- spdlog: ❌ 缺失（sysroot 和开发板都没有）

DRM 显示: card0 (HDMI-A-1, DSI-1), card1, renderD128/D129 ✅

### 17:00 — BufferPool dmabuf 独立验证

编写并运行独立测试程序 `tests/test_dmabuf_mpp.c`，交叉编译后部署到开发板：

1. **V4L2 采集**: 打开 `/dev/video0` → 设置 1920×1080 NV12 → REQBUFS(4) → mmap → EXPBUF 导出 dmabuf fd ✅
2. **MPP dmabuf 导入**: `mpp_buffer_import(&buf, &info)` with `MPP_BUFFER_TYPE_DRM` → 成功返回 ✅
3. **MMAP 数据验证**: NV12 帧数据正确（3.1MB），保存到 `/tmp/test_frame.nv12` ✅
4. **MppFrame 组装**: `mpp_frame_set_buffer(frame, mpp_buf)` 接受导入的 buffer → 成功 ✅
5. **地址对比**: V4L2 mmap `0x7f808c6000` vs MPP ptr `0x7f7fcd2000`（不同但正常——DMA-BUF 标准行为，底层共享物理内存）

**关键结论：V4L2 dmabuf → MPP 零拷贝路径完全可用！**

### 17:10 — 编码测试待解决

**test_mpp_enc.c 开发过程：**
1. 初始尝试：用 MPP task dequeue 方式 → `mpp_task_meta_get_frame` 返回 -1，不可用
2. 改为官方流程（参考 `external/mpp/test/mpi_enc_test.c`）：
   - `mpp_buffer_group_get_internal` → `mpp_buffer_get` → `mpp_frame_set_buffer` → `encode_put_frame` → `encode_get_packet`
   - MPP 初始化成功（buffer group + frm_buf + pkt_buf + md_buf 分配 OK）
   - Buffer stride 对齐：1920×1080 → hor=1920, ver=1088, size=3,133,440
3. 所有 set_s32 返回 0，但 `MPP_ENC_SET_CFG` 返回 -6（MPP_ERR_VALUE）——未解决，暂时跳过配置
4. `encode_put_frame` 持续段错误，与以下因素**无关**：
   - 编码配置参数（SET_CFG）
   - 预绑定 output packet（KEY_OUTPUT_PACKET meta）
   - V4L2 初始化顺序
   - Buffer 大小（对齐版本也段错误）
5. 段错误定位：memcpy OK, frame_init/set OK, meta OK → `encode_put_frame` → SIGSEGV

**推测原因：** MPP 库版本与内核 VPU 驱动不匹配，或编码器需要设备树中的特定配置。
**后续方案：** 尝试用 Rockchip 官方 `mpi_enc_test` 编译运行验证编码器本身是否正常。

**两个测试程序保留：**
- `tests/test_dmabuf_mpp.c` ✅ dmabuf 验证通过
- `tests/test_mpp_enc.c` ⚠️ 编码崩溃待排查

---

### 19:00 — MPP 编码 segfault 根因定位和修复

**根因：** `encode_put_frame` 段错误是因为没配置 `prep:hor_stride` 和 `prep:ver_stride`。加上后 SET_CFG 返回 0，编码正常。

**验证过程：**
1. 编译运行 `tests/test_mpp_real.c`：V4L2 实采帧 + MPP 编码 → `encode_put_frame ret=0`，产出 88KB H.264 ✅
2. 确认 H.264 文件头完整（SPS/PPS/SEI）
3. 将修复后的配置集成到 `mpp_encoder.cpp`
4. 修复 `MppApi` 类型冲突：我们的 `typedef MppApi_t* MppApi` 和系统 `typedef MppApi_t MppApi` 冲突 → 重命名为 `MppApiPtr`

### 18:35 — 画面色彩修复 + 崩溃修复

**色彩问题根因：** NV12 Y 平面 stride=2112≠width=1920，代码用 `w×h` 算 UV 偏移少了 207KB，读到了 Y 的 padding。

**修复：** 读取 V4L2 `bytesperline`(2112)，存入 FrameRef.stride，UV 偏移改为 `stride×h`。

**崩溃根因：** `glPixelStorei(GL_UNPACK_ROW_LENGTH, stride/2)` 给 UV 纹理传了 1056 像素/行，但实际 UV 每行只有 960 像素对（1920 字节），越界访问触发 Mali GPU 驱动 segfault。

**修复：** 去掉所有 `glPixelStorei` 调用。UV 是紧密排列的，不需要 row length。Y 的轻微 padding 可接受。

**实现方式：** 极简 RTSP/RTP 服务器（QTcpServer + RTP over TCP interleaved），零外部依赖。

**备选方案尝试：**
- live555 下载失败（服务器返回 HTML 非 gzip）
- MediaMTX ARM 下载被 GitHub 限速截断
- 最终采用自实现：Qt socket + RTSP 协议解析 + H.264 RTP 封包

**RTSP 协议：** OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN 完整实现 ✅

**RTP 封包：**
- 单 NAL 包模式（≤ MTU-12）
- FU-A 分片模式（> MTU-12）
- RTP over TCP interleaved 模式（穿越防火墙）✅
- RTP timestamp 递增修复 ✅

**SDP 配置：** sprop-parameter-sets 已添加 ✅

**当前问题：**
- VLC 握手成功（OPTIONS→DESCRIBE→SETUP→PLAY），TC PACK送 RTP 数据 ✅
- VLC 收到 RTCP 包返回 ✅
- SPS/PPS 缓存实现但有 bug：MPP 一帧含多个 NAL，缓存逻辑需分割 NAL ⚠️
- IDR 帧缓存为空（因分割错误）⚠️

**改动：**
- 重写 `mpp_encoder.h/.cpp`：QThread → QObject + QTimer（主线程驱动），正确的 buffer group 编码流程
- `main.cpp` 集成编码器：NAL callback 写入 `/tmp/encode_test.h264`

**运行结果（12秒）：**
- 采集 330+ 帧无崩溃 ✅
- MPP 编码器正常发心跳 ✅
- 产出 2.9MB 有效 H.264 文件 ✅
- 实时画面显示正常 ✅

**关键里程碑：**
1. Qt Wayland + OpenGL 窗口正常 ✅
2. Mali GPU NV12→RGB shader 渲染正确（静态测试卡验证） ✅
3. **V4L2 采集 + OpenGL 实时显示联调通过** ✅（采集帧1→displayQueue→mmap→GL渲染）
4. 全部模块联调通过 ✅：Capture + RtspServer + SegmentRecorder + Watchdog + PerfMonitor + MainWindow

**崩溃根因定位：**
- 多线程 V4L2 ioctl 导致 segfault → 改为 QTimer 主线程驱动解决
- QApplication 创建前使用 QTimer → 调整初始化顺序解决

**当前模块状态：**
| 模块 | 状态 | 备注 |
|------|------|------|
| 采集+显示 | ✅ | QTimer主线程驱动 |
| BufferPool | ✅ | 8 buf × 3.1MB, mmap+EXPBUF |
| Qt Wayland UI | ✅ | Mali GPU, OpenGL shader |
| Watchdog | ✅ | 心跳监控正常 |
| PerfMonitor | ✅ | FPS/延迟统计 |
| RTSP Server | ⚠️ | live555 待集成 |
| SegmentRecorder | ⚠️ | 逻辑就绪，MPP未通 |
| YOLO (video1) | ⚠️ | 打开失败 -EINVAL |
| MPP 编码 | ⚠️ | encode_put_frame segfault |
| RKNN 模型 | ❌ | 模型文件缺失 |

**spdlog 补充：**
- 通过 `curl` 下载 spdlog v1.12.0 tarball → 放入 `third_party/spdlog/spdlog/`
- GitHub git clone 方式失败（TLS 错误），改用 HTTP tarball

**交叉编译环境调试：**
1. Buildroot 工具链 POISON 检查阻止本机 Qt5 路径 → 通过 `-DQt5_DIR=...` 等指定 sysroot 路径解决
2. `stdlib.h` 找不到 → toolchain.cmake 添加 `CMAKE_CXX_FLAGS_INIT --sysroot=...` 解决
3. spdlog v1.12.0 对自定义类型（MPP_RET）编译报错 → 将所有 `spdlog::error("{}", ret)` 改为 `(int)ret`
4. MOC 生成 MppEncoder 元数据但 .cpp 未编译 → 暂时从 HEADERS 和 SOURCES 中移除 mpp_encoder
5. `config.h` 缺 `<cstddef>` → 补上
6. `buffer_pool.cpp` 类型名错误 → `v4l2_reqbufs` → `struct v4l2_requestbuffers`
7. `detector.h` rknn_context typedef 冲突 → 改为 `#include <rknn/rknn_api.h>`
8. `segment_recorder` 返回类型不一致 → 修正 `void` vs `bool`
9. `mainwindow.h` 槽函数签名 → 加 `bool` 参数
10. `perf_monitor` snapshot const 问题 → 去 const
11. `main.cpp` 中 MPP 相关代码全部注释掉

**编译成功：** `rk3568-camera` 二进制在 `app/build/rk3568-camera`

**当前功能状态：**
- ✅ 采集 + DisplayQueue/EncodeQueue (capture)
- ✅ BufferPool (buffer_pool)
- ✅ RTSP server 占位 (rtsp_server)
- ✅ 分段录制 (segment_recorder)
- ✅ YOLO 检测占位 (detector, 后处理 TODO)
- ✅ VideoWidget OpenGL 渲染 (dmabuf import TODO)
- ✅ Watchdog + PerfMonitor
- ⚠️ MPP 编码器 (暂时禁用，待排查 encode_put_frame 段错误)

---

### 18:35 — 画面色彩修复 + 崩溃修复

**色彩问题根因：** NV12 Y 平面 stride=2112≠width=1920，代码用 `w×h` 算 UV 偏移少了 207KB，读到了 Y 的 padding。

**修复：** 读取 V4L2 `bytesperline`(2112)，存入 FrameRef.stride，UV 偏移改为 `stride×h`。

**崩溃根因：** `glPixelStorei(GL_UNPACK_ROW_LENGTH, stride/2)` 给 UV 纹理传了 1056 像素/行，但实际 UV 每行只有 960 像素对（1920 字节），越界访问触发 Mali GPU 驱动 segfault。

**修复：** 去掉所有 `glPixelStorei` 调用。UV 是紧密排列的，不需要 row length。Y 的轻微 padding 可接受。

---

### 21:30 — 画面色彩问题排查与修复

**现象：** 采集到的 NV12 数据显示在屏幕上色彩错误（偏色）。

**数据链路：**
```
传感器 → ISP → /dev/video0 (mainpath, MPLANE模式)
  → V4L2 mmap buffer (3342336 bytes)
    ├─ Y  平面: offset 0,       stride=2112字节/行, 有效1920像素, 后192字节零填充
    └─ UV 平面: offset 2280960, stride=1920字节/行 (紧密排列, 无padding)
```

**根因：** 代码中 UV 偏移用 `data + width * height` (1920×1080=2073600)，但实际 UV 平面起始偏移是 `stride * height` (2112×1080=2280960)。差了 207360 字节，UV 数据读到了 Y 平面的 stride padding 区域（全零），导致色彩完全错误。

**定位方法：**
1. 在 `capture.cpp` 的 `configureFormat` 中通过 `VIDIOC_S_FMT` 后读取 `plane_fmt[0].bytesperline` = 2112（存入 `configStride_`）
2. 在采集第一帧时 dump 整个 mmap buffer (3342336 bytes) 到 `/tmp/frame_dump.nv12`
3. 用 Python 分析原始数据：检查 offset `w*h`(2073600) 处是全零（Y 的 padding），offset `stride*h`(2280960) 处是非零交替数据（UV 值），确认 UV 在 `stride*h` 处、紧密排列无 padding

**修复：** `renderRawNV12(const uint8_t *data, int w, int h, int stride)` — UV 偏移改为 `data + stride * h`。

---

### 21:40 — 画面卡住不流畅排查与修复

**现象：** 应用启动后只渲染几帧，然后画面完全卡住不动。

**性能数据定位：**
在 displayTimer 回调中加入 `QElapsedTimer` 每 2 秒统计：
```
前2秒: 5 renders, 177 empty polls, avg 5ms
后4秒: 0 renders, 201 empty polls — 采集完全停止！
```

**根因：** V4L2 buffer 泄漏。采集 tick 每次 `DQBUF` 获取 buffer，通过 `displayQueue` 传给显示线程，但显示回调消费后**未归还 V4L2 buffer**（没调 `VIDIOC_QBUF`）。8 个 buffer 全部被占满后，`DQBUF` 返回 `EAGAIN` 永久阻塞，采集停止。

**定位方法：**
1. 在 display 回调中加 `QElapsedTimer` 测量每次渲染耗时（发现仅 5ms，不是性能问题）
2. 加渲染计数和空轮询计数，发现 2 秒后渲染归零，采集完全停止
3. 检查 `BufferPool::release(ref)` 的调用链——`FrameRef` 只是 shared_ptr，析构不归还 buffer。需要在显式调用 `capture.pool().release(ref)` 才能 `VIDIOC_QBUF`

**修复：** 在 display 回调渲染后加 `capture.pool().release(ref)` 归还 buffer。

**验证结果：** 每 2 秒约 60 帧（30fps），平均渲染 5ms/帧，持续稳定。

---

### 最终采集+显示参数

| 参数 | 值 |
|------|-----|
| 格式 | NV12 (MPLANE, num_planes=1) |
| 分辨率 | 1920×1080 |
| Y stride | 2112 字节/行 (192 字节零填充) |
| UV 布局 | 紧密排列，offset=2280960 |
| 帧率 | 30fps (每2秒60帧) |
| 渲染耗时 | ~5ms/帧 |
| V4L2 buffers | 8 个, 每个 3342336 字节 |
| 显示 QTimer | 10ms 快速轮询, 批量取帧 |

---

### 5月14日 — stride=2112 根因定位与修复

#### 现象
采集 1920×1080 NV12 时，`bytesperline` 返回 2112（而非 1920），导致数据布局不一致：
- Y 平面 2112 字节/行（1920 有效 + 192 零填充）
- UV 平面 1920 字节/行（紧密排列）
- 显示端和编码端都需要额外的紧凑化处理

#### 排查过程

**1. 写 test_v4l2_info.c 全面查询 V4L2 设备信息**
- 默认格式：2112×1568 NV12, bpl=2112, sizeimage=4967424
- S_FMT 1920×1080 后：bpl=2112, sizeimage=3317760
- 4 种不同的 sizeimage 请求（2073600/2280960/3110400/3421440）全部返回 bpl=2112
- **关键数据：** dump 一帧后分析，offset w×h (2073600) 处全零，offset s×h (2280960) 处是 UV 数据

**2. 对比 test_v4l2_8001280.c 的发现**
- 800×1280 时 bpl=800，不是 2112！
- 该文件直接 S_FMT，没有先调 G_FMT

**3. G_FMT 对比测试（修改 test_v4l2_8001280.c）**
```
A: 不加G_FMT直接S_FMT 800×1280 → bpl=800 ✅
B: 先G_FMT再S_FMT 800×1280    → bpl=800 ✅
```
G_FMT 本身不改变 stride。

**4. 核心对"sizeimage"对 stride 的影响**
- 800×1280 sizeimage=0(不设)：bpl=800 ✅
- 800×1280 sizeimage=1536000：bpl=800 ✅
- 1920×1080 sizeimage=0(不设)：bpl=1920 ✅
- sizeimage 不是根因

**5. 反复测试后设备状态变化**
- 多次测试后，`default` 格式从 2112×1568 变为 1920×1080
- S_FMT 1920×1080 后 bpl=1920

#### 根因

在 `capture.cpp` 的 `configureFormat()` 中：

```cpp
VIDIOC_G_FMT → 读到旧格式的 bytesperline=2112（设备初始默认值）
       ↓
  只改了 width/height/sizeimage
  **没有清零 bytesperline！**
       ↓
VIDIOC_S_FMT → 驱动看到用户"请求"了 bytesperline=2112
       ↓
  驱动保留 2112，stride 一直是 2112
```

V4L2 规范：`VIDIOC_S_FMT` 时，如果 `bytesperline` 为非零值，驱动认为这是用户请求的 stride；如果为 0，驱动自动计算合适的 stride。

#### 修复

在 `capture.cpp` 第 53 行，G_FMT 之后、S_FMT 之前加一行：

```cpp
fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;  // 让驱动自己算
```

#### 连锁影响

修复后 stride=1920=width，数据紧密排列：
- Y 平面: 1920×1080 字节（无 padding）
- UV 平面: 紧接着 Y，1920×540 字节（无 padding）
- **不需要任何紧凑化处理**
- 显示端和编码端都可以直接用 mmap 地址 + width×height×3/2 大小

#### 验证

```
Format set: 1920x1080 stride=1920 NV12 @ 30fps ✅
Capture: frame 1 idx=0 ✅
```

### 12:58 — GitHub 推送

第七次推送：stride=2112 根因修复 + V4L2 测试工具
- capture.cpp: G_FMT 后显式清零 bytesperline，stride 从 2112 恢复 1920
- buffer_pool.cpp: release 改为 ref==0 精确判断
- 新增 tests/test_v4l2_info.c、test_v4l2_8001280.c
- GIT_LOG.md 同步更新

当前状态: 采集+显示正常（stride=1920 紧密排列），MPP 编码器待重新集成

### 15:34 — MPP 编码器 SET_CFG 修复

**问题：** `mppApi_->control(mppCtx_, MPP_ENC_SET_CFG, cfg)` 返回 -6。

**排查：** 对比 test_mpp_real.c 的配置，发现少了 `prep:hor_stride` 和 `prep:ver_stride` 参数。

**修复：** 补齐全部参数（和 test_mpp_real.c 完全一致）：
```
prep:width, prep:height, prep:hor_stride, prep:ver_stride, prep:format,
rc:mode, rc:bps_target, rc:gop, rc:fps_in_num/denorm, rc:fps_out_num/denorm
```
SET_CFG 返回 0。

**验证：** 编码 ~1170帧/40秒（30fps），编码文件正常产出，RTSP 服务器就绪。

### 15:38 — GitHub 第八次推送

提交 MPP 编码器 SET_CFG 修复 + 全链路联调代码。

### 16:15 — RTSP 延时测试与分析

**测试方法：** 屏幕秒表法——电脑开毫秒计时器，摄像头对准，VLC 画面时间差 ≈ 端到端延时。

**测试结果：** 延时约 3s，每次略有波动。

**延时组成：**
- 采集+编码+网络：~10ms（可忽略）
- **VLC 网络缓冲：~2000-3000ms（主要来源）**

**UDP 尝试：** VLC 首选 `RTP/AVP/TCP;interleaved=0-1`，不支持纯 UDP。保留 TCP。

**延时优化：**
- VLC 端：降低网络缓存到 200ms（设置 → 输入/编解码器 → 网络缓存）
- 代码层：TCP 可靠传输保证每帧到达，监控场景不需要丢旧帧

**帧率验证（3分钟测试）：**
- 编码帧率：30fps（1170帧/40秒）
- 显示帧率：~29fps（287-299帧/10秒）
- VLC 保活：每30秒发 OPTIONS，无断连

### 16:21 — GitHub 第九次推送

RTSP TCP/UDP 模式测试 + 延时分析记录。
