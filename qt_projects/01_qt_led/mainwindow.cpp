/*
 * MainWindow 实现文件
 *
 * 功能描述：
 *   这是一个运行在 RK3568 开发板上的 Qt 5 图形界面应用程序，
 *   通过 Linux sysfs 接口（/sys/class/leds/work/brightness）
 *   控制 GPIO0_C0 引脚上的 LED 灯的亮灭。
 *
 * 界面布局（800x1280 竖屏）：
 *   - 顶部：彩色装饰条 + 标题区（⚡ LED 控制面板）
 *   - 中上：LED 圆形指示器 + 状态文字 + 亮度进度条
 *   - 中部：设备信息卡片（显示设备名、引脚、触发模式）
 *   - 底部：开灯/关灯按钮 + 操作提示
 *   - 最底部：sysfs 路径提示 + 自动刷新状态
 *
 * 工作原理：
 *   1. 通过 QFile 读写 /sys/class/leds/work/brightness 文件
 *      - 写入 "255" 表示点亮 LED（GPIO 输出高电平）
 *      - 写入 "0"   表示熄灭 LED（GPIO 输出低电平）
 *   2. QTimer 每秒轮询一次亮度值，刷新界面状态显示
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFile>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QProgressBar>

/* ==================================================================== */
/*  LED sysfs 控制文件路径                                                */
/*  在 RK3568 上对应 GPIO0_C0 引脚的 work LED                            */
/*  /sys/class/leds/work/brightness 取值范围：0（灭）~ 255（最亮）       */
/* ==================================================================== */
const QString MainWindow::kLedBrightnessPath =
    QStringLiteral("/sys/class/leds/work/brightness");

/* ==================================================================== */
/*  构造函数：初始化窗口、构建 UI 布局、连接信号槽、启动定时监视          */
/* ==================================================================== */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    /* 加载 Qt Designer 生成的 UI 基础结构 */
    ui->setupUi(this);

    /* ---------------------------------------------------------------- */
    /*  设置窗口标题和全局样式表                                            */
    /*  背景色 #0f0f23（深蓝黑），所有控件文字默认白色                       */
    /* ---------------------------------------------------------------- */
    setWindowTitle("RK3568 LED 控制");
    setStyleSheet(
        "QMainWindow { background-color: #0f0f23; }"
        "QWidget { color: #ffffff; font-family: 'Noto Sans CJK SC', "
        "  'Microsoft YaHei', sans-serif; }"
    );

    /* ---------------------------------------------------------------- */
    /*  创建中央部件，设置为深蓝黑背景                                      */
    /* ---------------------------------------------------------------- */
    QWidget *central = new QWidget(this);
    central->setStyleSheet("background-color: #0f0f23;");
    setCentralWidget(central);

    /* ---------------------------------------------------------------- */
    /*  根布局：垂直排列，无边距                                           */
    /*  root 包含：顶部装饰条 + body（主内容区）                          */
    /* ---------------------------------------------------------------- */
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    /* ---------------------------------------------------------------- */
    /*  顶部彩色装饰条：5px 高的渐变色横条                                 */
    /*  颜色从左到右：青绿 → 浅青 → 蓝色                                  */
    /* ---------------------------------------------------------------- */
    QFrame *topLine = new QFrame();
    topLine->setFixedHeight(5);
    topLine->setStyleSheet(
        "background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "  stop:0 #00b894, stop:0.5 #00cec9, stop:1 #0984e3);"
        "border: none;"
    );
    root->addWidget(topLine);

    /* ---------------------------------------------------------------- */
    /*  body 布局：主内容区域，带边距，所有子区块按 stretch 比例分布       */
    /*  各区块的 stretch 比例：标题2 : LED区4 : 信息卡2 : 按钮3 : 底部1   */
    /*  总比例 12，stretch 越大占用垂直空间越多                           */
    /* ---------------------------------------------------------------- */
    QVBoxLayout *body = new QVBoxLayout();
    body->setContentsMargins(35, 15, 35, 20);
    body->setSpacing(0);
    root->addLayout(body);

    /* ================================================================ */
    /*  区块 1：标题区（stretch = 2）                                   */
    /*  包含：闪电图标、主标题、副标题、分隔线                            */
    /* ================================================================ */
    QVBoxLayout *titleSection = new QVBoxLayout();
    titleSection->setSpacing(4);
    titleSection->setAlignment(Qt::AlignCenter);

    /* 闪电图标 */
    QLabel *iconLabel = new QLabel("⚡");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(
        "font-size: 44px; color: #fdcb6e; background: transparent;"
    );
    titleSection->addWidget(iconLabel);

    /* 主标题文字 */
    QLabel *titleLabel = new QLabel("LED 控制面板");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "font-size: 40px; font-weight: bold; color: #dfe6e9;"
        "background: transparent;"
    );
    titleSection->addWidget(titleLabel);

    /* 副标题：显示平台信息 */
    QLabel *subtitleLabel = new QLabel("RK3568 · GPIO0_C0 (work)");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet(
        "font-size: 16px; color: #636e72; background: transparent;"
    );
    titleSection->addWidget(subtitleLabel);

    /* 分隔线：居中放置的短横线 */
    QFrame *div = new QFrame();
    div->setFixedHeight(2);
    div->setFixedWidth(160);
    div->setStyleSheet("background-color: #2d3436; border: none;");
    QHBoxLayout *dh = new QHBoxLayout();
    dh->addStretch();
    dh->addWidget(div);
    dh->addStretch();
    titleSection->addLayout(dh);

    /* 将标题区加入 body，stretch = 2 */
    body->addLayout(titleSection, 2);

    /* ================================================================ */
    /*  区块 2：LED 指示器区（stretch = 4，占用最大空间）               */
    /*  包含：圆形 LED 指示器、状态文字、亮度进度条                     */
    /* ================================================================ */
    QVBoxLayout *ledSection = new QVBoxLayout();
    ledSection->setSpacing(12);
    ledSection->setAlignment(Qt::AlignCenter);

    /* 圆形 LED 指示器：170x170 像素，圆角半径 85px 形成正圆 */
    ledIndicator = new QWidget();
    ledIndicator->setFixedSize(170, 170);
    ledIndicator->setStyleSheet(
        "background-color: #2d3436; border-radius: 85px;"
        "border: 5px solid #636e72;"
    );
    ledSection->addWidget(ledIndicator, 0, Qt::AlignCenter);

    /* 状态文字：显示 "已开启" 或 "已关闭" */
    statusLabel = new QLabel("状态: 未知");
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet(
        "font-size: 28px; font-weight: bold; color: #b2bec3;"
        "background: transparent;"
    );
    ledSection->addWidget(statusLabel);

    /* 亮度进度条行：太阳图标 + 进度条 + 数值标签 */
    QHBoxLayout *brightRow = new QHBoxLayout();
    brightRow->setContentsMargins(20, 0, 20, 0);

    /* 太阳图标 */
    QLabel *sunIcon = new QLabel("☀");
    sunIcon->setStyleSheet(
        "font-size: 18px; color: #636e72; background: transparent;"
    );
    brightRow->addWidget(sunIcon);

    /* 亮度进度条：范围 0~255，隐藏百分比文字 */
    QProgressBar *brightnessBar = new QProgressBar();
    brightnessBar->setObjectName("brightnessBar");
    brightnessBar->setRange(0, 255);
    brightnessBar->setValue(0);
    brightnessBar->setFixedHeight(12);
    brightnessBar->setTextVisible(false);
    brightnessBar->setStyleSheet(
        "QProgressBar { background-color: #2d3436; border: none;"
        "  border-radius: 6px; }"
        "QProgressBar::chunk {"
        "  background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #00b894, stop:1 #00cec9);"
        "  border-radius: 6px;"
        "}"
    );
    brightRow->addWidget(brightnessBar);

    /* 亮度数值标签（固定宽度 38px，右对齐） */
    brightnessLabel = new QLabel("0");
    brightnessLabel->setFixedWidth(38);
    brightnessLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    brightnessLabel->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #b2bec3;"
        "background: transparent;"
    );
    brightRow->addWidget(brightnessLabel);

    ledSection->addLayout(brightRow);

    /* 将 LED 区加入 body，stretch = 4 */
    body->addLayout(ledSection, 4);

    /* ================================================================ */
    /*  区块 3：设备信息卡片（stretch = 2）                             */
    /*  一个带圆角和边框的信息卡片，显示设备名、引脚、触发模式           */
    /* ================================================================ */
    QVBoxLayout *infoSection = new QVBoxLayout();
    infoSection->setSpacing(6);
    infoSection->setAlignment(Qt::AlignCenter);

    /* 信息卡片容器：深色背景、圆角、边框 */
    QFrame *infoCard = new QFrame();
    infoCard->setStyleSheet(
        "QFrame { background-color: #1a1a35; border-radius: 12px;"
        "  border: 1px solid #2d3436; }"
    );
    QVBoxLayout *cardLayout = new QVBoxLayout(infoCard);
    cardLayout->setContentsMargins(25, 12, 25, 12);
    cardLayout->setSpacing(4);

    /* Lambda 表达式：添加一行信息（标签 + 值，左右分开） */
    auto addInfoRow = [&](const QString &label, const QString &value) {
        QHBoxLayout *row = new QHBoxLayout();
        QLabel *l = new QLabel(label);
        l->setStyleSheet(
            "font-size: 14px; color: #636e72; background: transparent;"
        );
        row->addWidget(l);
        row->addStretch();
        QLabel *v = new QLabel(value);
        v->setStyleSheet(
            "font-size: 14px; color: #dfe6e9; background: transparent;"
        );
        row->addWidget(v);
        cardLayout->addLayout(row);
    };

    /* 添加三行设备信息 */
    addInfoRow("设备", "RK3568 ATK-DLRK3568");
    addInfoRow("LED 引脚", "GPIO0_C0");
    addInfoRow("触发模式", "none (手动控制)");

    infoSection->addWidget(infoCard);

    /* 将信息卡片区加入 body，stretch = 2 */
    body->addLayout(infoSection, 2);

    /* ================================================================ */
    /*  区块 4：按钮区（stretch = 3）                                   */
    /*  包含：开灯/关灯两个大按钮 + 操作提示文字                        */
    /* ================================================================ */
    QVBoxLayout *btnSection = new QVBoxLayout();
    btnSection->setSpacing(0);
    btnSection->setAlignment(Qt::AlignCenter);

    /* 按钮水平行 */
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(20);

    /* 开灯按钮：绿色渐变，78px 高，16px 圆角 */
    onButton = new QPushButton("开 灯");
    onButton->setFixedHeight(78);
    onButton->setStyleSheet(
        "QPushButton {"
        "  font-size: 28px; font-weight: bold; color: white;"
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #00b894, stop:1 #008e6f);"
        "  border-radius: 16px; border: none; padding: 8px;"
        "}"
        "QPushButton:pressed {"
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #008e6f, stop:1 #007a5e);"
        "}"
        "QPushButton:disabled { background-color: #2d3436; color: #636e72; }"
    );
    btnRow->addWidget(onButton);

    /* 关灯按钮：红色渐变，78px 高，16px 圆角 */
    offButton = new QPushButton("关 灯");
    offButton->setFixedHeight(78);
    offButton->setStyleSheet(
        "QPushButton {"
        "  font-size: 28px; font-weight: bold; color: white;"
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #d63031, stop:1 #b71c1c);"
        "  border-radius: 16px; border: none; padding: 8px;"
        "}"
        "QPushButton:pressed {"
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #b71c1c, stop:1 #a01818);"
        "}"
        "QPushButton:disabled { background-color: #2d3436; color: #636e72; }"
    );
    btnRow->addWidget(offButton);

    btnSection->addLayout(btnRow);

    /* 操作提示文字 */
    QLabel *hint = new QLabel("提示：按下后 LED 状态立即更新");
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(
        "font-size: 12px; color: #2d3436; background: transparent;"
        "padding-top: 6px;"
    );
    btnSection->addWidget(hint);

    /* 将按钮区加入 body，stretch = 3 */
    body->addLayout(btnSection, 3);

    /* ================================================================ */
    /*  区块 5：底部提示区（stretch = 1，最小比例）                    */
    /*  显示 sysfs 路径和自动刷新状态                                    */
    /* ================================================================ */
    QVBoxLayout *footerSection = new QVBoxLayout();
    QLabel *footerLabel = new QLabel("/sys/class/leds/work  |  自动刷新中");
    footerLabel->setAlignment(Qt::AlignCenter);
    footerLabel->setStyleSheet(
        "font-size: 11px; color: #2d3436; background: transparent;"
    );
    footerSection->addWidget(footerLabel);
    body->addLayout(footerSection, 1);

    /* ---------------------------------------------------------------- */
    /*  信号-槽连接                                                    */
    /* ---------------------------------------------------------------- */
    /* 开灯按钮点击 → 调用 turnOnLed() */
    connect(onButton, &QPushButton::clicked,
            this, &MainWindow::turnOnLed);

    /* 关灯按钮点击 → 调用 turnOffLed() */
    connect(offButton, &QPushButton::clicked,
            this, &MainWindow::turnOffLed);

    /* ---------------------------------------------------------------- */
    /*  定时器：每秒刷新一次 LED 状态显示                                */
    /* ---------------------------------------------------------------- */
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout,
            this, &MainWindow::refreshStatus);
    timer->start(1000);

    /* 首次刷新状态 */
    refreshStatus();
}

/* ==================================================================== */
/*  析构函数：释放 UI 资源                                              */
/* ==================================================================== */
MainWindow::~MainWindow()
{
    delete ui;
}

/* ==================================================================== */
/*  showEvent：窗口显示时自动全屏                                        */
/*  重写 QMainWindow::showEvent，在窗口显示后立即调用 showFullScreen()  */
/* ==================================================================== */
void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    showFullScreen();
}

/* ==================================================================== */
/*  setLedBrightness(int value)：设置 LED 亮度                          */
/*  功能：将亮度值写入 sysfs 控制文件                                    */
/*  参数：value = 0（灭）~ 255（最亮）                                  */
/*  实现：以只写模式打开 kLedBrightnessPath，写入数值后关闭文件          */
/* ==================================================================== */
void MainWindow::setLedBrightness(int value)
{
    QFile file(kLedBrightnessPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << value;
    file.close();
}

/* ==================================================================== */
/*  getLedBrightness()：读取当前 LED 亮度                               */
/*  功能：从 sysfs 文件读取当前亮度值                                    */
/*  返回值：0~255 表示当前亮度；-1 表示读取失败（文件无法打开）         */
/*  实现：以只读模式打开文件，读取整数后关闭                             */
/* ==================================================================== */
int MainWindow::getLedBrightness()
{
    QFile file(kLedBrightnessPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;

    QTextStream in(&file);
    int value;
    in >> value;
    file.close();
    return value;
}

/* ==================================================================== */
/*  turnOnLed()：打开 LED                                               */
/*  设置亮度为 255（最亮），然后刷新界面显示                             */
/* ==================================================================== */
void MainWindow::turnOnLed()
{
    setLedBrightness(255);
    refreshStatus();
}

/* ==================================================================== */
/*  turnOffLed()：关闭 LED                                              */
/*  设置亮度为 0（灭），然后刷新界面显示                                 */
/* ==================================================================== */
void MainWindow::turnOffLed()
{
    setLedBrightness(0);
    refreshStatus();
}

/* ==================================================================== */
/*  updateLedStyle(bool on)：更新 LED 指示器的外观样式                  */
/*  功能：根据 LED 亮灭状态，切换圆形指示器的颜色和发光效果              */
/*  参数：on = true  → 亮（绿色径向渐变 + 绿色边框）                   */
/*        on = false → 灭（灰色径向渐变 + 灰色边框）                   */
/* ==================================================================== */
void MainWindow::updateLedStyle(bool on)
{
    if (on) {
        /* LED 点亮：绿色径向渐变（从亮绿到深绿） */
        ledIndicator->setStyleSheet(
            "background-color: qradialgradient(cx:0.4, cy:0.4, radius:0.5,"
            "  fx:0.3, fy:0.3, stop:0 #55efc4, stop:0.6 #00b894,"
            "  stop:1 #008e6f);"
            "border-radius: 85px; border: 5px solid #00b894;"
        );
    } else {
        /* LED 熄灭：灰色径向渐变（从浅灰到深灰再到背景色） */
        ledIndicator->setStyleSheet(
            "background-color: qradialgradient(cx:0.4, cy:0.4, radius:0.5,"
            "  fx:0.3, fy:0.3, stop:0 #636e72, stop:0.6 #2d3436,"
            "  stop:1 #1a1a2e);"
            "border-radius: 85px; border: 5px solid #636e72;"
        );
    }
}

/* ==================================================================== */
/*  refreshStatus()：刷新界面显示状态                                    */
/*  功能：读取当前 LED 亮度值，更新所有 UI 控件显示                      */
/*  调用频率：由 QTimer 每 1000ms 触发一次                              */
/* ==================================================================== */
void MainWindow::refreshStatus()
{
    /* 读取当前亮度值 */
    int brightness = getLedBrightness();

    /* 查找亮度进度条控件（通过 ObjectName） */
    QProgressBar *brightnessBar = findChild<QProgressBar*>("brightnessBar");

    /* ---------------------------------------------------------------- */
    /*  错误处理：无法读取亮度值时显示错误状态                           */
    /* ---------------------------------------------------------------- */
    if (brightness < 0) {
        statusLabel->setText("状态: 错误");
        statusLabel->setStyleSheet(
            "font-size: 28px; font-weight: bold; color: #d63031;"
            "background: transparent;"
        );
        brightnessLabel->setText("ERR");
        onButton->setEnabled(false);
        offButton->setEnabled(false);
        updateLedStyle(false);
        if (brightnessBar) brightnessBar->setValue(0);
        return;
    }

    /* 正常状态：启用按钮 */
    onButton->setEnabled(true);
    offButton->setEnabled(true);

    /* 更新亮度数值显示 */
    brightnessLabel->setText(QString::number(brightness));
    if (brightnessBar) brightnessBar->setValue(brightness);

    /* ---------------------------------------------------------------- */
    /*  根据亮度值切换状态文字和指示器样式                                */
    /*  亮度 > 0 视为"已开启"，否则为"已关闭"                           */
    /* ---------------------------------------------------------------- */
    if (brightness > 0) {
        statusLabel->setText("状态: 已开启");
        statusLabel->setStyleSheet(
            "font-size: 28px; font-weight: bold; color: #00b894;"
            "background: transparent;"
        );
        updateLedStyle(true);
    } else {
        statusLabel->setText("状态: 已关闭");
        statusLabel->setStyleSheet(
            "font-size: 28px; font-weight: bold; color: #636e72;"
            "background: transparent;"
        );
        updateLedStyle(false);
    }
}
