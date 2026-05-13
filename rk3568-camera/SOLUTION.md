# RK3568 智能摄像头方案 (v2)

## 一、项目概述

基于 RK3568 + OV13850 的智能摄像头系统，实现：

- 视频采集（OV13850 MIPI CSI）
- 本地屏幕实时显示
- RTSP 网络推流
- 本地 MP4 分段录制保存
- Qt 展示界面
- YOLOv5s 目标检测（NPU 推理）

## 二、技术选型

| 模块 | 技术选型 | 说明 |
|------|---------|------|
| 摄像头 | OV13850 (MIPI CSI) | 13MP 摄像头模组 |
| 操作系统 | Buildroot | Rockchip SDK，轻量嵌入式 Linux |
| 视频采集 | V4L2 mmap + DMA-BUF | 零拷贝，绕过 FFmpeg 采集层 |
| 硬编码 | **MPP 原生 API** | RK3568 VPU 硬件编码 H.264 |
| 帧分发 | **BufferPool + 双队列** | 一入多出 + DMA-BUF fd 引用计数 |
| RTSP 推流 | **live555** (内嵌) | C++ 原生 RTSP Server，与 Qt 编译在一起 |
| 本地保存 | FFmpeg `avformat` (MP4) | MPP 编码包 → 分段 MP4（5分钟/段） |
| 显示 | Qt5 `QOpenGLWidget` + NV12 Shader | 通过 EGL_EXT_image_dma_buf_import 导入 dmabuf |
| YOLO | RKNN + YOLOv5s | NPU 推理，1 TOPS 算力 |
| UI 框架 | Qt5 Widgets | 工具栏 + 状态栏 + 视频预览 + 性能监控 |
| 时间基准 | **CLOCK_MONOTONIC** | 全系统统一 PTS |
| 构建系统 | CMake | 交叉编译 aarch64 |
| 日志 | spdlog | 分级日志，文件轮转 |

## 三、ISP 双路径架构

### 3.1 设备节点说明

| 节点 | 名称 | 用途 | 本项目角色 |
|------|------|------|-----------|
| `/dev/video0` | rkisp_mainpath | ISP 主输出 | **采集 1920×1080 NV12 → 显示 + 编码** |
| `/dev/video1` | rkisp_selfpath | ISP 自路径 | **采集 640×640 NV12 → YOLO 推理** |
| `/dev/video2` | rkisp_rawwr0 | RAW 写回 0 | 不使用 |
| `/dev/video3` | rkisp_rawwr2 | RAW 写回 2 | 不使用 |
| `/dev/video4` | rkisp_rawwr3 | RAW 写回 3 | 不使用 |
| `/dev/video5` | rkisp_iqtool | IQ 调试 | 不使用 |
| `/dev/video6` | rkisp_rawrd0_m | RAW 读回 0 (Output) | 不使用 |
| `/dev/video7` | rkisp_rawrd2_s | RAW 读回 2 (Output) | 不使用 |
| `/dev/video8` | rkisp-statistics | 3A 统计信息 (Metadata) | 不使用 |
| `/dev/video9` | rkisp-input-params | ISP 参数 (Metadata) | 不使用 |
| `/dev/video-enc0` | MPP Encoder | 硬件编码器 | MPP API 调用，不直接操作节点 |
| `/dev/video-dec0` | MPP Decoder | 硬件解码器 | 不使用 |

### 3.2 数据流（v2 零拷贝 + 一入多出）

```
                              OV13850 (CSI)
                                    │
                              ┌─────▼──────┐
                              │  RK ISP     │
                              │ 硬件内部分流 │
                              └──┬───────┬──┘
                                 │       │
              video0 (mainpath)  │       │  video1 (selfpath)
              1920×1080 NV12     │       │  640×640 NV12
              V4L2 mmap buffer   │       │  V4L2 mmap buffer
                  │              │       │
        ┌─────────▼──────────┐   │  ┌────▼───────────┐
        │   BufferPool        │   │  │ DetectThread    │
        │   (dmabuf fd +      │   │  │ RKNN YOLOv5s   │
        │    ref_count)       │   │  │ NPU 推理        │
        │   ┌────┴────┐       │   │  └────┬───────────┘
        │   ▼         ▼       │   │       │
        │ DisplayQ  EncodeQ   │   │  detectionReady()
        │ (深度1,   (深度4,   │   │   Qt 信号
        │ 丢旧帧)   不丢帧)  │   │       │
        └──┬─────────┬───────┘   │       ▼
           │         │           │  Qt 主线程画框
  ┌────────▼──┐ ┌───▼──────────┐ │  叠加到 VideoWidget
  │ Display   │ │ MPP 硬编码    │ │  (通过 dmabuf
  │ Timer     │ │ H.264 裸码流  │ │  零拷贝渲染)
  │ dmabuf→GL │ └───┬───────┬───┘
  │ →Qt界面  │     │       │
  └──────────┘     ▼       ▼
              live555   FFmpeg
              RTSP      MP4分段
              :8554     5min/段
```

### 3.3 零拷贝路径说明

```
V4L2 mmap buffer (DDR 物理内存)
    │
    ├──→ dmabuf fd ──→  EGL_EXT_image_dma_buf_import ──→ OpenGL 纹理 (GPU)
    │                    (Qt VideoWidget 直接渲染，不走 CPU)
    │
    ├──→ dmabuf fd ──→  MPP MppBuffer (VPU)
    │                    (硬编码直接读 DDR，不走 CPU)
    │
    └──→ dmabuf fd ──→  RGA (2D 加速器，缩放/格式转换)
                         (如需要 NV12→RGB 用于 YOLO 预处理)
```

### 3.4 双路径优势

| 对比项 | 单路径（video0取帧→RGA缩放） | 双路径（video0+video1硬件分流） |
|--------|------------------------------|----------------------------------|
| 缩放开销 | RGA 硬件 ~2ms/帧 | **ISP 内部零开销** |
| 内存拷贝 | 多一次 | **零拷贝（ISP DMA 直出）** |
| 管道延迟 | ~8ms | **~3ms** |
| DDR 带宽占用 | ~90MB/s (memcpy) | **0 MB/s (dmabuf 引用传递)** |
| YOLO 帧争抢 | 和显示/编码抢队列 | **独立管道，完全解耦** |

## 四、线程模型

```
线程              数据源         职责                          帧率
─────────────────────────────────────────────────────────────────
CaptureThread     video0         V4L2 dequeue → BufferPool       30fps
                                  入 DisplayQueue + EncodeQueue
EncodeThread      EncodeQueue     MPP 硬编码 → H264NALQueue      30fps
DetectThread      video1 直读     640×640 → RKNN 推理            10-15fps(跳帧)
RtspServer        独立事件循环    live555 RTSP 服务               独立
WatchdogThread    各线程心跳      异常检测+自动恢复               1Hz
主线程 (Qt)       DisplayQueue +  OpenGL 渲染 + YOLO 画框        30fps
                  检测信号        + 性能监控 UI 刷新
```

**关键差异：** v2 取消了"显示和编码抢同一个队列"的问题。采集线程把同一帧的 dmabuf fd 分别推入 DisplayQueue 和 EncodeQueue，队列间互不影响。

## 五、项目目录结构

```
rk3568-camera/
├── app/                               # 应用源码
│   ├── CMakeLists.txt                 # CMake 构建配置
│   ├── main.cpp                       # 入口，启动各线程
│   ├── mainwindow.h / .cpp            # Qt 主窗口 + 工具栏控制
│   ├── capture.h / .cpp               # V4L2 mmap 采集 → BufferPool + 双队列分发
│   ├── buffer_pool.h / .cpp           # DMA-BUF BufferPool（dmabuf fd + 引用计数）
│   ├── frame_ref.h                    # FrameRef 统一帧描述结构
│   ├── spsc_queue.h                   # 无锁单产单消队列（DisplayQueue/EncodeQueue）
│   ├── pts_clock.h                    # 统一时间戳 (CLOCK_MONOTONIC)
│   ├── mpp_encoder.h / .cpp           # MPP 原生 API 硬编码 H.264
│   ├── rtsp_server.h / .cpp           # live555 RTSP 内嵌服务
│   ├── segment_recorder.h / .cpp      # FFmpeg 分段 MP4 录制（5min/段）
│   ├── detector.h / .cpp              # 独立读 video1 → RKNN YOLOv5s
│   ├── video_widget.h / .cpp          # QOpenGLWidget dmabuf 渲染 + 画框叠加
│   ├── watchdog.h / .cpp              # 线程心跳监控 + 异常自恢复
│   ├── perf_monitor.h / .cpp          # 性能监控（FPS/队列深度/延迟/温度）
│   └── config.h                       # 编译期配置常量（分辨率、分段时长等）
│
├── third_party/                       # 第三方库
│   └── live555/                       # live555 源码（RTSP Server）
│
├── model/                             # 模型文件
│   ├── yolov5s.onnx                   # 原始 ONNX 模型
│   ├── yolov5s.rknn                   # 转换后 RKNN 模型（INT8 量化）
│   └── coco_labels.txt                # COCO 标签文件
│
├── scripts/                           # 工具脚本
│   ├── convert_model.sh               # ONNX → RKNN 转换脚本
│   ├── build.sh                       # 交叉编译脚本
│   └── deploy.sh                      # 部署到开发板脚本
│
├── buildroot/                         # Buildroot 外部配置
│   └── configs/
│       └── rk3568_camera_defconfig    # Buildroot 配置片段
│
└── README.md                          # 项目说明
```

## 六、核心模块设计

### 6.1 FrameRef（统一帧描述结构）

替代到处传 `AVFrame*`，用统一的轻量结构，只传 dmabuf fd 和元数据。

```cpp
struct FrameRef {
    int         dmabufFd;       // DMA-BUF 文件描述符（真正的数据）
    uint64_t    pts;            // 统一时间戳 (CLOCK_MONOTONIC，微秒)
    uint32_t    width;
    uint32_t    height;
    uint32_t    format;         // V4L2_PIX_FMT_NV12
    uint32_t    size;           // buffer 字节数
    uint32_t    v4l2Index;      // V4L2 buffer index（用于归还）
    int         v4l2Fd;         // V4L2 设备 fd（用于 VIDIOC_QBUF 归还）
    std::atomic<int> refCount;  // 引用计数
};

using FrameRefPtr = std::shared_ptr<FrameRef>;  // 或手动管理引用计数
```

### 6.2 PtsClock（统一时间戳）

全系统使用 `CLOCK_MONOTONIC` 作为唯一时间基准，所有模块（采集、显示、编码、RTSP、录制）共用。

```cpp
class PtsClock {
public:
    static uint64_t nowUs() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL
             + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
    }

    static uint64_t nowMs() { return nowUs() / 1000ULL; }

    // 计算帧间隔（微秒）
    static uint64_t deltaUs(uint64_t prevPts) { return nowUs() - prevPts; }
};
```

### 6.3 BufferPool（DMA-BUF 零拷贝管理）

V4L2 初始化时分配 N 个 mmap buffer，导出 dmabuf fd。全程不拷贝帧数据，只传 dmabuf fd + 引用计数。

```cpp
class BufferPool {
public:
    // 初始化：V4L2 REQBUFS + mmap + EXPBUF
    bool init(int v4l2Fd, int numBuffers, uint32_t width, uint32_t height);

    // 采集线程：dequeue 一个 buffer，分配 FrameRef（refCount = 2，给显示+编码）
    FrameRefPtr acquire(uint32_t index);

    // 消费者：减少引用计数，归零时还回 V4L2
    void release(FrameRefPtr ref);

    // V4L2 dequeue/queue 原始操作
    bool dequeueBuffer(uint32_t &index);
    bool queueBuffer(uint32_t index);

private:
    int                   v4l2Fd;
    int                   numBuffers;
    std::vector<int>      dmabufFds;     // 每个 buffer 的 dmabuf fd
    std::vector<void*>    mmapAddrs;     // 每个 buffer 的 mmap 地址
    std::vector<uint32_t> bufferSizes;
};
```

### 6.4 SPSCQueue（无锁单产单消队列）

DisplayQueue（深度1，丢旧帧保证低延迟）和 EncodeQueue（深度4，不丢帧）都使用此模板。

```cpp
template<typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity);

    // 生产者（Capture 线程调用）
    bool tryPush(T item);     // DisplayQueue 用，满则丢弃
    bool push(T item);        // EncodeQueue 用，满则阻塞

    // 消费者
    T pop();                  // 阻塞等待
    T tryPop();               // 非阻塞，空则返回 nullptr
    size_t sizeApprox() const;

private:
    std::vector<T>              buffer;
    std::atomic<size_t>         writePos {0};
    std::atomic<size_t>         readPos  {0};
    size_t                      mask;
    std::mutex                  mtx;
    std::condition_variable     cv;
};
```

### 6.5 CaptureThread（采集线程 v2）

使用 V4L2 原生 API（非 FFmpeg），mmap 方式采集，dmabuf 零拷贝分发到双队列。

```cpp
class CaptureThread : public QThread {
    Q_OBJECT
public:
    bool open(const char *device, uint32_t width, uint32_t height, uint32_t fps);
    void stop();

    SPSCQueue<FrameRefPtr>& getDisplayQueue();   // 显示消费
    SPSCQueue<FrameRefPtr>& getEncodeQueue();     // 编码消费

signals:
    void error(const QString &msg);
    void heartbeat();          // watchdog 心跳

protected:
    void run() override {
        while (running) {
            // 1. V4L2 dequeue buffer
            uint32_t index;
            if (pool.dequeueBuffer(index) < 0) { handleError(); continue; }

            // 2. 从 BufferPool 获取 FrameRef（refCount = 2）
            auto ref = pool.acquire(index);
            ref->pts = PtsClock::nowUs();

            // 3. 一入多出：推入两个独立队列
            displayQueue.tryPush(ref);   // 深度1，丢掉旧帧
            encodeQueue.push(ref);       // 深度4，确保不丢帧

            emit heartbeat();
        }
    }

private:
    int                     v4l2Fd;
    BufferPool              pool;
    SPSCQueue<FrameRefPtr>  displayQueue {1};   // 显示只保留最新帧
    SPSCQueue<FrameRefPtr>  encodeQueue  {4};   // 编码缓冲4帧
    bool                    running = false;
};
```

### 6.6 MppEncoder（硬编码线程 v2）

从 EncodeQueue 取 FrameRef，通过 dmabuf fd 交给 MPP 硬编码，产出 H.264 NAL 单元。

```cpp
class MppEncoder : public QThread {
public:
    void start(SPSCQueue<FrameRefPtr> &encodeQueue);
    void stop();

    // NAL 回调：编码产出的 H.264 NAL 单元
    using NalCallback = std::function<void(const uint8_t* data, size_t len, uint64_t pts)>;
    void setNalCallback(NalCallback cb);

signals:
    void heartbeat();

protected:
    void run() override {
        while (running) {
            auto ref = encodeQueue.pop();
            if (!ref) continue;

            // 用 dmabuf fd 导入 MPP（零拷贝）
            MppBuffer mppBuf;
            mpp_buffer_import(&mppBuf, ref->dmabufFd, ref->size);

            MppFrame frame;
            mpp_frame_init(&frame);
            mpp_frame_set_buffer(frame, mppBuf);
            mpp_frame_set_pts(frame, ref->pts);   // 统一时间戳
            // ... 送入 MPP 编码 ...

            // NAL 回调（每产出一个 NAL）
            for (auto &nal : outputNals) {
                nalCallback(nal.data, nal.size, nal.pts);
            }

            // 归还 BufferPool
            pool.release(ref);
            emit heartbeat();
        }
    }

private:
    SPSCQueue<FrameRefPtr> *encodeQueue;
    MppCtx                   mppCtx;
    MppApi                  *mppApi;
    NalCallback              nalCallback;
    BufferPool              &pool;          // 用于归还 buffer
    bool                     running = false;
};
```

### 6.7 SegmentRecorder（分段 MP4 录制）

每 5 分钟自动切换文件，防止单文件过大和断电丢失。

```cpp
class SegmentRecorder {
public:
    // basePath: 基础路径，如 "/data/record"
    // segmentSec: 分段时长，默认 300 秒（5分钟）
    bool start(const char *basePath, int segmentSec = 300);
    bool stop();
    bool isRecording() const;

    // MPP 编码线程回调中调用
    void writePacket(AVPacket *pkt);

signals:
    void segmentStarted(const QString &filePath);
    void segmentFinished(const QString &filePath);

private:
    void rotateSegment();                                   // 切换文件
    QString generateFileName();
    void cleanupOldSegments(int keepCount = 120);           // 保留最近 10 小时

    AVFormatContext *outCtx      = nullptr;
    AVStream        *videoStream = nullptr;
    int64_t          segmentStartPts = 0;
    uint64_t         segmentStartMs  = 0;   // CLOCK_MONOTONIC，用于计时
    char             basePath[256];
    int              segmentSec    = 300;   // 5 分钟
    int              segmentIndex  = 0;
    bool             recording     = false;
};
```

### 6.8 RtspServer（live555 内嵌，保持不变）

```cpp
class RtspServer {
public:
    bool start(int port = 8554, const char *streamName = "/live");
    void stop();

    // MPP 编码后调用，H.264 NAL + 统一时间戳
    void feedNALU(const uint8_t *data, size_t len, uint64_t pts);

private:
    TaskScheduler    *scheduler  = nullptr;
    UsageEnvironment *env        = nullptr;
    RTSPServer       *rtspServer = nullptr;
};
```

### 6.9 DetectThread（YOLO 推理线程，独立 video1）

从 `/dev/video1` 独立读取 640×640 NV12 帧，直接送入 NPU。

```cpp
class DetectThread : public QThread {
    Q_OBJECT
public:
    bool open(const char *device, int width, int height);
    bool loadModel(const char *rknnPath);
    void stop();

signals:
    void detectionReady(QVector<Detection> results);
    void heartbeat();

protected:
    void run() override;

private:
    AVFormatContext *fmtCtx   = nullptr;
    rknn_context      rknnCtx = 0;
    bool              running = false;

    void preprocess(AVFrame *frame, uint8_t *inputBuf);
    QVector<Detection> postProcess(float *output, int width, int height);
};

struct Detection {
    QRect    bbox;
    int      classId;
    QString  className;
    float    confidence;
};
```

### 6.10 VideoWidget（Qt dmabuf 渲染 + 框叠加）

通过 `EGL_EXT_image_dma_buf_import` 扩展，将 dmabuf fd 直接导入 OpenGL 纹理，零拷贝上屏。

```cpp
class VideoWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    void renderFrame(FrameRefPtr ref);               // dmabuf → GL 纹理
    void setDetections(QVector<Detection> det);

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    // 通过 EGL_EXT_image_dma_buf_import 导入 dmabuf
    void importDmaBuf(int dmabufFd, GLuint &texY, GLuint &texUV);

    GLuint                texY  = 0;
    GLuint                texUV = 0;
    QOpenGLShaderProgram *program = nullptr;
    QVector<Detection>    detections;
    QMutex                detMutex;
};
```

### 6.11 Watchdog（异常监控与自恢复）

```cpp
class Watchdog : public QObject {
    Q_OBJECT
public:
    // 注册一个监控目标（通常是某个线程）
    // name: 名称, timeoutMs: 超时毫秒, recoverFn: 恢复回调
    void registerTarget(const QString &name, int timeoutMs,
                        std::function<void()> recoverFn);

    // 被监控线程定期调用
    void feed(const QString &name);

    void start();
    void stop();

signals:
    void targetTimeout(const QString &name);

private:
    struct Target {
        QString              name;
        int                  timeoutMs;
        uint64_t             lastHeartbeat;
        std::function<void()> recoverFn;
    };
    QList<Target> targets;
    QTimer        checkTimer;    // 每秒检查一次
};
```

### 6.12 状态机

```cpp
// 系统标志位（可叠加状态）
enum SystemFlag {
    FLAG_IDLE      = 0,
    FLAG_PREVIEW   = 1 << 0,   // 采集+显示运行中
    FLAG_STREAMING = 1 << 1,   // RTSP 推流中
    FLAG_RECORDING = 1 << 2,   // 本地录制中
    FLAG_AI        = 1 << 3,   // YOLO 检测中
    FLAG_ERROR     = 1 << 4,   // 异常状态
};

class SystemState {
public:
    void set(quint32 flag);
    void clear(quint32 flag);
    bool test(quint32 flag) const;
    quint32 flags() const;

signals:
    void changed(quint32 oldFlags, quint32 newFlags);

private:
    std::atomic<quint32> m_flags {FLAG_IDLE};
};
```

### 6.13 PerfMonitor（性能监控）

```cpp
struct PerfStats {
    float captureFps      = 0;
    float displayFps      = 0;
    float encodeFps       = 0;
    float detectFps       = 0;
    int   displayQueueDepth = 0;
    int   encodeQueueDepth  = 0;
    float encodeLatencyMs = 0;    // 编码单帧耗时
    float detectLatencyMs = 0;    // 推理单帧耗时
    float cpuTemp         = 0;    // /sys/class/thermal/
};

class PerfMonitor : public QObject {
    Q_OBJECT
public:
    void start(int intervalMs = 1000);   // 每秒统计一次

    void tickCapture();      // Capture 线程每帧调用
    void tickDisplay();
    void tickEncode(float latencyMs);
    void tickDetect(float latencyMs);

    PerfStats snapshot() const;

signals:
    void statsUpdated(PerfStats stats);

private:
    QTimer timer;
    // 各计数器（原子操作）
    std::atomic<int> captureCount {0};
    std::atomic<int> displayCount {0};
    std::atomic<int> encodeCount  {0};
    std::atomic<int> detectCount  {0};
    // ...
};
```

### 6.14 main.cpp 启动流程

```cpp
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // spdlog 初始化
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Camera system starting...");

    MainWindow window;

    // 1. 系统状态机
    SystemState sysState;

    // 2. 采集线程
    CaptureThread capture;
    if (!capture.open("/dev/video0", 1920, 1080, 30)) {
        spdlog::error("Failed to open /dev/video0");
        return -1;
    }

    // 3. MPP 编码线程
    MppEncoder encoder;
    encoder.start(capture.getEncodeQueue());

    // 4. RTSP 服务
    RtspServer rtsp;
    rtsp.start(8554, "/live");
    encoder.setNalCallback([&](const uint8_t* d, size_t n, uint64_t pts) {
        rtsp.feedNALU(d, n, pts);
    });

    // 5. 分段录制（5分钟/段，保留最近120段=10小时）
    SegmentRecorder recorder;
    encoder.setRecordCallback([&](AVPacket *pkt) {
        if (recorder.isRecording())
            recorder.writePacket(pkt);
    });

    // 6. YOLO 检测（独立 video1）
    DetectThread detector;
    detector.loadModel("model/yolov5s.rknn");
    detector.open("video4linux2:/dev/video1", 640, 640);

    // 7. 显示驱动（从 DisplayQueue 取帧）
    QTimer displayTimer;
    QObject::connect(&displayTimer, &QTimer::timeout, [&]() {
        auto ref = capture.getDisplayQueue().tryPop();
        if (ref) {
            window.videoWidget()->renderFrame(ref);
        }
    });
    displayTimer.start(33);

    // 8. Watchdog
    Watchdog watchdog;
    watchdog.registerTarget("capture",  2000, [&]{ capture.restart(); });
    watchdog.registerTarget("encoder",  3000, [&]{ encoder.restart(); });
    watchdog.registerTarget("detector", 5000, [&]{ detector.restart(); });
    watchdog.start();

    // 信号连接
    QObject::connect(&capture,  &CaptureThread::heartbeat,
                     [&] { watchdog.feed("capture"); });
    QObject::connect(&encoder,  &MppEncoder::heartbeat,
                     [&] { watchdog.feed("encoder"); });
    QObject::connect(&detector, &DetectThread::heartbeat,
                     [&] { watchdog.feed("detector"); });
    QObject::connect(&detector, &DetectThread::detectionReady,
                     &window,   &MainWindow::onDetection);
    QObject::connect(&sysState, &SystemState::changed,
                     &window,   &MainWindow::onStateChanged);

    // 启动
    capture.start();
    encoder.start();
    detector.start();
    sysState.set(FLAG_PREVIEW);

    window.show();
    return app.exec();
}
```

## 七、FFmpeg 在本方案中的职责边界 (v2)

```
FFmpeg 负责:                         不负责:
├── avformat MP4 分段封装              ├── V4L2 采集 (改用原生 API)
├── 编码包时间基转换                    ├── H.264 硬编码 (MPP 做)
├── MP4 muxer 文件管理                 ├── RTSP 服务 (live555 做)
└── 文件头/尾、moov atom 写入          ├── 显示渲染 (Qt/OpenGL 做)
                                       ├── 帧分发 (BufferPool + 双队列做)
                                       └── YOLO 推理 (RKNN 做)
```

## 八、RTSP 推流方案

```
                       ┌─────────────────────┐
 MPP 编码产出 ──────▶  │   live555 内嵌服务   │  ◀─── VLC 拉流
 H.264 NAL + PTS      │                     │  rtsp://192.168.x.x:8554/live
                       │  NALQueue (PTS排序) │
                       │       │             │
                       │  ┌────▼──────────┐  │
                       │  │FramedSource    │  │
                       │  │(从队列取 NAL)   │  │
                       │  └────┬──────────┘  │
                       │       ▼             │
                       │  H264RTPSink        │
                       │       │             │
                       │  RTSPServer :8554   │
                       └─────────────────────┘
```

## 九、YOLOv5s RKNN 部署流程

```
① 训练/获取 YOLOv5s 模型 (PyTorch)
       ↓
② 导出为 ONNX 格式
       ↓
③ rknn-toolkit2 转换 → yolov5s.rknn（INT8 量化）
       ↓
④ 部署到 RK3568，用 librknnrt.so 推理
```

**NPU 性能预估（YOLOv5s-640×640 INT8）：**

| 指标 | 预估值 |
|------|--------|
| 推理时间 | 25-35 ms |
| 可达帧率 | 10-15 fps（含预处理/后处理） |
| CPU 占用 | <10%（NPU 独立运行） |

## 十、Buildroot 关键依赖

```makefile
# V4L2 驱动（内核配置）
# CONFIG_VIDEO_OV13850=y

# FFmpeg（仅用于 MP4 分段封装）
BR2_PACKAGE_FFMPEG=y
BR2_PACKAGE_FFMPEG_AVFORMAT=y
BR2_PACKAGE_FFMPEG_AVCODEC=y

# MPP 硬编码库
BR2_PACKAGE_ROCKCHIP_MPP=y

# 2D 加速（NV12→RGB 转换加速，可选）
BR2_PACKAGE_ROCKCHIP_RGA=y

# NPU 运行时
BR2_PACKAGE_RKNN_RUNTIME=y

# Qt5 基础 + OpenGL + EGL
BR2_PACKAGE_QT5=y
BR2_PACKAGE_QT5BASE_WIDGETS=y
BR2_PACKAGE_QT5BASE_OPENGL=y
BR2_PACKAGE_QT5BASE_OPENGL_LIB=y
BR2_PACKAGE_QT5BASE_EGLFS=y

# EGL dmabuf 扩展（Qt 零拷贝渲染依赖）
# BR2_PACKAGE_LIBDRM=y

# spdlog（日志）
BR2_PACKAGE_SPDLOG=y

# live555（需自定义 Buildroot 包或手动编译）
# BR2_PACKAGE_LIVE555=y
```

## 十一、开发分步计划

| # | 阶段 | 内容 | 验证标准 |
|---|------|------|---------|
| 1 | ISP 验证 | video0 出 1920×1080 NV12、video1 出 640×640 NV12，确认 dmabuf 可用 | `v4l2-ctl --stream-mmap` 抓到正确帧 |
| 2 | BufferPool | V4L2 mmap + EXPBUF → dmabuf fd，引用计数归还 | dmabuf fd 可被 MPP/GL 导入 |
| 3 | 采集+显示 | Capture → DisplayQueue → Qt dmabuf→EGL 渲染 | 界面实时出 1080p，CPU 占用 <5% |
| 4 | MPP 编码 | EncodeQueue → MPP 硬编码（dmabuf 导入）→ H.264 | ffplay 能播，延迟 <50ms |
| 5 | RTSP 推流 | live555 内嵌 + NAL feed → VLC 拉流 | `rtsp://IP:8554/live` 稳定播放 |
| 6 | 分段录制 | FFmpeg MP4 分段封装（5min/段），自动轮转 | 正常 MP4 文件，切换无黑帧 |
| 7 | YOLO 集成 | video1 独立读 → RKNN → 检测 → 画框 | 界面显示检测框 |
| 8 | Watchdog | 线程心跳 + 超时恢复 + 摄像头重连 | 拔插摄像头自动恢复 |
| 9 | 性能监控 | FPS/队列深度/编码耗时/推理耗时/NPU温度 UI | 监控面板实时刷新 |
| 10 | 联调 | 全功能串联 + 状态机 + 异常路径测试 | 持续运行 24h 无崩溃 |

## 十二、v1 → v2 核心变更

| v1 (旧方案) | v2 (新方案) | 变更原因 |
|-------------|------------|---------|
| FFmpeg `av_read_frame` 采集 | V4L2 原生 `mmap` + `EXPBUF` | dmabuf 零拷贝必须用原生 API |
| `AVFrame` 深拷贝入队列 | `FrameRef` (dmabuf fd + 引用计数) | 消除 ~90MB/s memcpy |
| 一个 FrameQueue 一入二出 | DisplayQueue(深度1) + EncodeQueue(深度4) | 显示和编码不再抢帧 |
| 各自时间戳 | `PtsClock` CLOCK_MONOTONIC 全系统统一 | RTSP 防 jitter、MP4 防不同步 |
| 单文件 MP4 | 5 分钟分段 MP4 | 防大文件和断电丢失 |
| 无 watchdog | Watchdog 线程心跳 + 自恢复 | 工业级稳定性 |
| 无状态机 | SystemState 标志位管理 | 状态转换可追踪 |
| 无日志系统 | spdlog | 崩溃可排查 |
| 无性能监控 | PerfMonitor | 实时发现瓶颈 |

## 十三、风险与应对

| 风险点 | 应对方案 |
|--------|---------|
| video0/video1 能否同时开启 | ISP 双路径硬件独立，已验证可行 |
| EGL dmabuf import 是否可用 | 检查 Mali GPU 驱动版本支持 EGLEXT |
| MPP 能否导入 dmabuf fd | `mpp_buffer_import` 接口验证 |
| dmabuf fd 跨线程传递安全性 | 每个消费者独立 dup() fd，用完 close() |
| OV13850 驱动未启用 | 检查 Buildroot 内核 `CONFIG_VIDEO_OV13850` |
| NPU DDR 带宽与 VPU 争抢 | YOLO 跳帧检测（每 3 帧推理 1 次） |
| live555 编译依赖 | 放入 `third_party/`，CMake 直接编译进项目 |
| Qt EGLFS 显示后端兼容 | 指定 `QT_QPA_PLATFORM=eglfs`，验证 DRM/KMS 驱动 |
