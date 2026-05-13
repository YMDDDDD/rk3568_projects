

# 但是我也要指出几个关键问题（很重要）

# 问题1：FrameQueue 设计还不够工业级

你现在：

```cpp
std::deque<AVFrame*>
```

问题：

# AVFrame 深拷贝成本太高

尤其：

```text
1080P NV12
```

一帧：

```text
~3MB
```

30fps：

```text
90MB/s memcpy
```

长期：

* cache miss
* DDR 压力
* 发热

都会明显。

---

# 我建议：

# 改 DMA-BUF + BufferPool

即：

```text
V4L2 mmap buffer
```

直接引用。

不要：

```text
av_frame_clone
memcpy
```

---

# 最优路线：

```text
V4L2
  ↓ dmabuf fd
MPP
Qt(OpenGL)
RGA
```

共享 buffer。

---

# 问题2：显示线程不要 tryPop 编码队列

这里有一个隐藏 bug。

你现在：

```cpp
encoder.pop()
display.tryPop()
```

实际上：

# 会竞争同一个队列

这意味着：

```text
显示拿走了
编码线程就没拿到
```

或者反过来。

---

# 正确方案：

应该：

# 一入多出

即：

```text
Capture
   │
   ├── DisplayQueue
   ├── EncodeQueue
   └── AIQueue
```

不是：

```text
多个消费者抢一个队列
```

---

# 这是你当前方案最大的问题之一。

---

# 问题3：live555 未来维护成本高

这个其实见仁见智。

live555：

## 优点：

* 稳
* 老牌
* 纯 RTSP

但：

## 缺点：

* API 非现代 C++
* 很难维护
* 调试痛苦
* 多客户端复杂

---

# 我更推荐：

## MediaMTX

原因：

| 项目     | MediaMTX | live555 |
| ------ | -------- | ------- |
| 维护难度   | 低        | 高       |
| RTSP   | √        | √       |
| RTMP   | √        | ×       |
| WebRTC | √        | ×       |
| HLS    | √        | ×       |
| 多客户端   | 强        | 一般      |
| 调试     | 容易       | 痛苦      |

---

# 我建议：

## 本地跑 MediaMTX

MPP 编码后：

```text
FFmpeg 推 localhost RTSP
```

更省事。

---

# 问题4：你现在没有“统一时间戳”

这是后期一定踩坑的。

---

# 当前：

```text
Qt显示
RTSP
MP4
YOLO
```

各自时间线。

---

# 正确做法：

统一：

```text
monotonic clock
```

或者：

```text
CLOCK_MONOTONIC_RAW
```

作为：

```text
全系统 PTS
```

否则：

* MP4 音画不同步
* RTSP jitter
* YOLO 框错位

都会出现。

---

# 问题5：没有 watchdog

工业项目必须有：

# pipeline 自恢复

例如：

* camera 掉线
* RTSP client 卡死
* MPP reset
* Qt 崩溃

否则：

```text
跑几天就死
```

---

# 问题6：YOLOv5s 对 RK3568 偏重

其实：

```text
YOLOv5n
YOLOv8n
PP-YOLOE-s
```

会更适合。

---

# YOLOv5s：

虽然能跑：

```text
10~15fps
```

但：

* DDR 压力大
* NPU 占用高
* 温度更高

---

# 更推荐：

```text
YOLOv5n INT8
```

---

# 问题7：缺少“录像分段”

现在：

```text
record.mp4
```

长时间运行：

* 文件巨大
* 断电风险
* FAT/exFAT 风险

---

# 工业方案：

应该：

```text
5min 一个文件
```

或者：

```text
segment muxer
```

---

# 我最推荐你增加的东西

# 一、BufferPool

比 FrameQueue 更重要。

---

# 二、统一 MediaPacket

不要到处：

```text
AVFrame
AVPacket
```

乱飞。

建议：

```cpp
struct MediaFrame {
    uint64_t pts;
    int width;
    int height;
    int format;
    int dmabuf_fd;
}
```

---

# 三、状态机

不要：

```text
start stop
```

乱调。

应该：

```text
Idle
Preview
Streaming
Recording
AI
```

状态机管理。

---

# 四、日志系统

必须：

```text
spdlog
```

或者：

```text
glog
```

否则后期崩溃很难查。

---

# 五、性能监控

建议：

```text
FPS
DDR
温度
队列深度
编码耗时
推理耗时
```

实时显示。

---

# 我对这个方案的最终评价

# 这是：

## “非常靠谱”的 RK3568 项目架构

尤其：

```text
ISP双路径
MPP原生
Qt OpenGL
RKNN
```

这些核心路线：

都是对的。

---

# 我认为如果优化后：

它完全可以变成：

* IPC 摄像头
* AI Box
* NVR
* 边缘 AI 网关
* 工业视觉终端

级别的产品。

---

# 我给你的最终建议（重要）

# 保留：

✅ ISP 双路径
✅ MPP 原生编码
✅ Qt OpenGL
✅ FFmpeg 只 mux
✅ YOLO 独立线程

---

# 重点修改：

❗ Queue 改一入多出
❗ 去掉 AVFrame 深拷贝
❗ 引入 DMA-BUF
❗ 增加统一时间戳
❗ 增加 watchdog
❗ MP4 分段录像

---

# 最后一句

这个方案：

```text
已经不是“能不能做”
而是“怎么做成工业级”
```

的阶段了。
