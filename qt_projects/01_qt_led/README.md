# RK3568 LED 控制面板

## 项目简介

一个运行在 RK3568 开发板上的 Qt 5 图形界面应用程序，通过 Linux **sysfs 接口**控制板载 GPIO LED 的亮灭。

| 项目 | 说明 |
|------|------|
| 目标硬件 | RK3568 ATK-DLRK3568（正点原子） |
| 显示分辨率 | 800×1280 DSI 竖屏 |
| 控制的 LED | `GPIO0_C0` → `/sys/class/leds/work/brightness` |
| 框架 | Qt 5.15.8 (Widgets) |
| 显示后端 | Wayland (Weston) |

---

## 功能说明

1. **开灯** — 向 sysfs 写入 `255`，GPIO 输出高电平，LED 点亮
2. **关灯** — 向 sysfs 写入 `0`，GPIO 输出低电平，LED 熄灭
3. **实时状态刷新** — 定时器每秒轮询亮度值，同步更新界面
4. **圆形 LED 指示器** — 亮时显示绿色径向渐变发光效果，灭时显示灰色
5. **亮度进度条** — 直观显示当前亮度值（0~255）
6. **设备信息卡片** — 显示开发板型号、LED 引脚号、触发模式
7. **全屏显示** — 窗口启动后自动全屏适配 800×1280 屏幕

### 设备树依赖

该程序依赖设备树中已使能的 `work` LED 节点：

```dts
&work_led {
    status = "okay";
};
```

对应 `rk3568-evb.dtsi` 中的定义：

```dts
leds: leds {
    compatible = "gpio-leds";
    work_led: work {
        gpios = <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>;
        linux,default-trigger = "heartbeat";
    };
};
```

---

## 编译步骤

### 1. 设置交叉编译环境

SDK 路径以 `/home/dxy/linux/rk3568_sdk` 为例，请替换为你实际的 SDK 路径。

```bash
export PATH="/path/to/rk3568_sdk/buildroot/output/rockchip_atk_dlrk3568/host/bin:$PATH"
```

### 2. 进入项目目录

```bash
cd /path/to/led
```

### 3. 生成 Makefile

```bash
qmake
```

`qmake` 来自 buildroot 交叉编译工具链，会自动检测目标架构为 aarch64。

### 4. 编译

```bash
make clean
make -j$(nproc)
```

编译产物为 ELF 可执行文件 `led`，目标架构 ARM64（aarch64）。

### 5. （可选）清理编译临时文件

```bash
make clean
```

---

## 部署与执行

### 1. 确保开发板已通过 ADB 连接

```bash
adb devices
```

输出应显示设备序列号且状态为 `device`。

### 2. 推送可执行文件到开发板

```bash
adb push led /data/
```

### 3. 设置 Wayland 环境变量并运行

```bash
adb shell "WAYLAND_DISPLAY=wayland-0 /data/led"
```

首次运行会输出 Mali GPU 驱动版本信息，随后 GUI 窗口出现在屏幕上。

> **说明**：开发板使用 Weston 作为 Wayland 合成器，因此需要设置 `WAYLAND_DISPLAY=wayland-0` 环境变量告知 Qt 使用 Wayland 后端。该环境变量的值可通过 `ls /run/wayland-*` 在板端确认。

---

## 截屏方法

由于 Weston 安全策略限制，`weston-screenshooter` 无法直接使用。可通过 GStreamer 的 `kmssrc` 元素捕获 DRM 显示输出：

```bash
# 先让 Qt 程序在后台运行
adb shell "WAYLAND_DISPLAY=wayland-0 /data/led" &

# 等待程序完全渲染
sleep 3

# 用 kmssrc 截取一帧并保存为 PNG
adb shell "gst-launch-1.0 kmssrc num-buffers=1 ! \
    videoconvert ! pngenc ! filesink location=/tmp/screenshot.png"

# 拉取到主机
adb pull /tmp/screenshot.png
```

> **注意**：`kmssrc` 截取的是 DRM 扫描输出缓冲区，在 Wayland 合成环境下可能与实际窗口内容不同步。最可靠的方式仍是直接观察屏幕。

---

## 项目文件结构

```
led/
├── led.pro              # Qt 项目文件
├── main.cpp             # 程序入口
├── mainwindow.h         # 主窗口头文件（含中文注释）
├── mainwindow.cpp       # 主窗口实现（含中文注释）
├── mainwindow.ui        # Qt Designer UI 文件
└── README.md            # 本文件
```

### 关键类说明

| 类 | 文件 | 职责 |
|---|---|---|
| `MainWindow` | `mainwindow.h/cpp` | 主窗口，负责 UI 构建、LED 控制、状态刷新 |
| 无（main 函数） | `main.cpp` | 创建 QApplication 并显示 MainWindow |

### 核心函数

| 函数 | 说明 |
|---|---|
| `setLedBrightness(int)` | 向 sysfs 写入亮度值（0~255） |
| `getLedBrightness()` | 从 sysfs 读取当前亮度值 |
| `turnOnLed()` | 设置亮度为 255 并刷新界面 |
| `turnOffLed()` | 设置亮度为 0 并刷新界面 |
| `refreshStatus()` | 定时器回调，读取亮度并更新 UI 控件 |
| `updateLedStyle(bool)` | 切换圆形指示器的亮/灭样式 |
| `showEvent()` | 窗口显示后自动全屏 |

---

## 常见问题

### Q: LED 不亮？

- 检查设备树中 `work` LED 节点是否使能：`cat /proc/device-tree/leds/work/status` 应为 `okay`
- 检查 GPIO 极性：如果硬件电路是低电平点亮，需要将设备树中的 `GPIO_ACTIVE_HIGH` 改为 `GPIO_ACTIVE_LOW`
- 直接测试 sysfs：`echo 255 > /sys/class/leds/work/brightness`

### Q: 屏幕没有显示 GUI？

- 确认 Weston 正在运行：`ps aux | grep weston`
- 确认 WAYLAND_DISPLAY 环境变量正确
- 确认显示分辨率匹配：`cat /sys/class/drm/card0-DSI-1/modes`

### Q: 编译报错找不到 Qt 头文件？

- 确认 `qmake` 来自 buildroot 交叉编译工具链，而非主机自带的 Qt
- 检查 PATH 环境变量是否正确指向 buildroot 的 `host/bin` 目录
