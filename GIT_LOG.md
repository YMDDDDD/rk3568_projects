# Git 推送记录

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
