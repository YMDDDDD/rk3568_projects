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

## 2026-05-13 第六次推送

### 推送内容

修复 `rk3568-camera` 项目中采集显示的两个关键 bug。

### 修复的问题

1. **色彩错误** — NV12 UV 平面起始偏移修正
   - 根因: `renderRawNV12` 中 UV 偏移用 `w*h`，但实际 V4L2 MPLANE 模式下 UV 在 `stride*h`
   - 修复: 增加 stride 参数，UV 偏移改为 `data + stride * h`

2. **画面卡住不动** — V4L2 buffer 泄漏
   - 根因: displayQueue 取帧消费后未调用 `BufferPool::release()` 归还，8 个 buffer 耗尽后 DQBUF 返回 EAGAIN
   - 修复: 渲染后调用 `capture.pool().release(ref)` 归还 buffer
   - 验证: 每 2 秒 60 帧(30fps)，渲染 ~5ms/帧

### 使用的 git 命令

```bash
git add rk3568-camera/ suggest.md
git commit -m "fix: 修复采集显示 stride 色彩偏差和 V4L2 buffer 泄漏导致画面卡住"
git push
```
