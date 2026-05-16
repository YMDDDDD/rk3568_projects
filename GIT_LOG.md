# Git 推送记录

## 2026-05-13 第五次推送

### 推送内容

提交 `rk3568-camera/` 智能摄像头项目，包含：

```
rk3568-camera/
├── app/                           # 应用源码 (32 文件)
│   ├── CMakeLists.txt             # CMake 交叉编译配置
│   ├── toolchain.cmake            # 工具链文件
│   ├── main.cpp                   # 入口（当前为采集+显示模式）
│   ├── mainwindow.h/cpp           # Qt 主窗口
│   ├── capture.h/cpp              # V4L2 采集（主线程 QTimer）
│   ├── video_widget.h/cpp         # OpenGL NV12 渲染
│   ├── buffer_pool.h/cpp          # DMA-BUF BufferPool
│   ├── frame_ref.h                # 统一帧描述结构
│   ├── spsc_queue.h               # 无锁单产单消队列
│   ├── pts_clock.h                # 统一时间戳
│   ├── config.h                   # 编译期配置
│   ├── mpp_encoder.h/cpp          # MPP H.264 硬编码
│   ├── rtsp_server.h/cpp          # 自实现 RTSP/RTP 服务器
│   ├── segment_recorder.h/cpp     # FFmpeg 分段 MP4 录制
│   ├── detector.h/cpp             # RKNN YOLO 检测
│   ├── watchdog.h/cpp             # 线程监控+自恢复
│   └── perf_monitor.h/cpp         # 性能统计
├── third_party/spdlog/            # spdlog v1.12.0 (header-only)
├── model/                         # 模型文件（coco_labels.txt）
├── scripts/                       # build/deploy/convert_model 脚本
├── buildroot/configs/             # Buildroot defconfig 片段
├── SOLUTION.md                    # 完整方案设计文档
├── WORKLOG.md                     # 工作日志
└── README.md                      # 项目说明
```

### 已排除

- `app/build/` — CMake 编译产物
- `tests/test_*` — 测试二进制文件

### 使用的 git 命令

```bash
# 追加 .gitignore 规则
echo "...build/..." >> .gitignore

# 添加文件
git add rk3568-camera/ .gitignore

# 提交
git commit -m "feat: RK3568 camera project - capture, display, MPP encode, RTSP push"

# 推送
git push origin main
```

---

## 2026-05-11 第一次推送

### 推送内容

首次初始化仓库，提交 `qt_projects/01_qt_led/` 项目。

包含文件：

```
qt_projects/01_qt_led/
├── main.cpp             程序入口
├── mainwindow.h         主窗口头文件
├── mainwindow.cpp       主窗口实现
├── mainwindow.ui        UI 布局文件
├── led.pro              Qt 项目配置
├── README.md            项目说明文档
```

### 使用的 git 命令

```bash
# 创建 .gitignore，忽略编译产物和临时文件
echo "..." > .gitignore

# 添加文件到暂存区
git add .gitignore README.md qt_projects/01_qt_led/

# 提交
git commit -m "init: RK3568 LED 控制面板 (Qt 5)"

# 关联远程仓库
git remote add origin git@github.com:YMDDDDD/rk3568_projects.git

# 推送到 GitHub
git push -u origin main
```

## 2026-05-11 第二次推送

### 推送内容

1. 完善 `.gitignore`，移除对 `Makefile` 和 `screenshot.png` 的忽略，新增构建目录忽略规则
2. 提交 LED 项目的 `Makefile`（qmake 生成的构建配置）
3. 提交 LED 项目截图 `screenshot.png`

### 使用的 git 命令

```bash
# 修改 .gitignore
#   移除: Makefile, screenshot.png
#   新增: qt_projects/build-*/, qt_projects/hello_world/, qt_projects/untitled/, 01_qt_test/

# 添加文件到暂存区
git add .gitignore qt_projects/01_qt_led/Makefile qt_projects/01_qt_led/screenshot.png

# 提交
git commit -m "chore: 提交 Makefile 和项目截图，完善 .gitignore 忽略规则"

# 推送到远程
git push
```

## 2026-05-11 第三次推送

### 推送内容

1. 创建项目级 skill 文件 `.opencode/skills/git-commit-rules/SKILL.md`，规定了 git 提交 8 步流程、GIT_LOG.md 和 README.md 的用途

### 使用的 git 命令

```bash
# 添加文件到暂存区
git add .opencode/

# 提交
git commit -m "chore: 添加项目 git 提交规则 skill"

# 推送到远程
git push
```

## 2026-05-11 第四次推送

### 推送内容

1. 将 `Makefile` 重新加入 `.gitignore` 忽略规则
2. 用 `git rm --cached` 从版本库中移除 Makefile 的跟踪，保留工作区文件
3. 修正了第二次推送时错误提交编译产物的问题

### 使用的 git 命令

```bash
# 从暂存区移除 Makefile（保留工作区文件）
git rm --cached qt_projects/01_qt_led/Makefile

# 添加文件到暂存区
git add .gitignore GIT_LOG.md

# 提交
git commit -m "fix: 将 Makefile 移出版本控制（qmake 编译产物不应提交）"

# 推送到远程
git push
```

## 2026-05-14 第七次推送

### 推送内容

修复 stride=2112 根因，恢复数据紧密排列。

### 修复的问题

**stride 固定 2112 → 修复后正确为 1920**
- 根因: `capture.cpp` 的 `configureFormat()` 中 `G_FMT` 读取旧格式 `bytesperline=2112`，未清零就传给 `S_FMT`，RK ISP 驱动保留了旧 stride
- 修复: `G_FMT` 后加 `fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0` 让驱动自动计算

**BufferPool 重复归还**
- `release()` 中 `ref->ref() <= 0` 改为 `== 0`，防止第二次 release 重复 queue

**新增测试工具**
- `tests/test_v4l2_info.c`: 全面 V4L2 设备信息查询（格式、尺寸、stride、buffer 分配、帧抓取分析）
- `tests/test_v4l2_8001280.c`: 800×1280 格式 + G_FMT 影响对比测试

### 使用的 git 命令

```bash
git add rk3568-camera/
git commit -m "fix: stride=2112根因——G_FMT带回旧bytesperline导致，加清零修复"
git push
```

## 2026-05-14 第八次推送

### 推送内容

修复 MPP 编码器 SET_CFG 返回 -6 的问题，恢复全链路联调。

### 修复的问题

**MPP SET_CFG 返回 -6**
- 根因: 缺少 `prep:hor_stride` 和 `prep:ver_stride` 参数
- 修复: 补齐全部编码配置参数（和 test_mpp_real.c 完全一致）
- SET_CFG 返回 0，编码器正常运行

**当前全链路状态**
- 采集 30fps（stride=1920 紧密排列）
- 显示 ~290帧/10秒
- 编码 30fps
- RTSP 服务器就绪

### 使用的 git 命令

```bash
git add rk3568-camera/
git commit -m "fix: MPP编码器SET_CFG修复——补齐prep:hor_stride/ver_stride参数"
git push
```

## 2026-05-14 第十次推送

### 推送内容

1. **video1 双路采集修复** — detector 改用原生 V4L2 API，video0/video1 可同时工作
2. **分段录像** — SegmentRecorder 存 .h264 裸流，5分钟一段，路径 `/userdata/records/`
3. **界面录像控制** — 按钮+文件列表+回放面板
4. **WiFi 自动重连** — 守护进程 + 开机自启脚本

### 使用的 git 命令

```bash
git add rk3568-camera/
git commit -m "fix: video1双路采集修复 + 分段录像 + 界面录像回放列表"
git push
```

## 2026-05-15 第十一次推送

### 推送内容

BufferPool 引用计数重构：shared_ptr 自定义 deleter 自动归还 V4L2 buffer。

### 修复的问题（5 项）
1. refCount=1 但有两个消费者（display+encoder）
2. release() 中 TOCTOU 竞态条件
3. 手动 release 分散且重复归还风险
4. shared_ptr + FrameRef::refCount 双重引用计数
5. acquire 末尾三行重复 return ref

### 修复方案
- FrameRef 删除 refCount 成员
- acquire 用 `new FrameRef()` + 自定义 deleter, 最后一个消费者析构时自动 QBUF
- 删除 BufferPool::release
- 删除 main.cpp / mpp_encoder.cpp 中的手动 release

### 使用的 git 命令
```bash
git add rk3568-camera/
git commit -m "refactor: BufferPool shared_ptr自定义deleter自动归还(buffer)"
git push
```

---

## 2026-05-16 第十三次推送

### 推送内容

**标题：** `perf: 编码+显示双路径 dma-buf 零拷贝，省去每帧 ~6MB CPU memcpy`

### 变更文件
- `app/buffer_pool.h` + `.cpp`：新增 `dmabufFd(index)` 公开 EXPBUF fd；PTS 移入 `acquire()` 内部
- `app/mpp_encoder.h` + `.cpp`：`init()` 预导入 8 个 V4L2 dma-buf → MppBuffer 映射表；`encodeOneFrame()` 删除 3MB memcpy，改用 `mpp_frame_set_buffer(importedMppBuf)` 走 MPP_EXT_DMA
- `app/video_widget.h` + `.cpp`：新增 `renderDmaBuf()`，用 `eglCreateImageKHR(LINUX_DMA_BUF)` + `glEGLImageTargetTexture2DOES` 直接导入 dmabuf 为 GL 纹理，替代 `glTexImage2D` 的 CPU→GPU 拷贝
- `app/main.cpp`：编码调用传 `capture.pool()`，显示调用改为 `renderDmaBuf(ref->dmabufFd, ...)`
- `app/capture.cpp`：删除手动 PTS 赋值（已移入 BufferPool）
- `WORKLOG.md`：追加 5/16 编译部署、MPP 零拷贝、显示零拷贝记录

### 为什么改
- 编码路径原先每帧 `memcpy(dst, mmapAddr, 3MB)`，`FrameRef::dmabufFd` 形同虚设
- 显示路径原先每帧 `glTexImage2D(mmapAddr)` 做 CPU→GPU 3MB 拷贝
- 两条路径合计浪费每帧 ~6MB CPU 带宽，改为 dma-buf 直通后 ISP→VPU/GPU 全程硬件 DMA

### 数据流
```
改前: V4L2 mmap → 编码: memcpy 3MB → MPP buf → VPU
                 显示: glTexImage2D 3MB → GL纹理 → GPU
改后: V4L2 dma-buf ─→ 编码: mpp_buffer_import → VPU (零拷贝)
                  └→ 显示: EGL dmabuf import → GPU (零拷贝)
```

### 使用的 git 命令
```bash
git add rk3568-camera/
git commit -m "perf: 编码+显示双路径 dma-buf 零拷贝"
git push
```
