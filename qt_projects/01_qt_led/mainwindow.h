#ifndef MAINWINDOW_H
#define MAINWINDOW_H

/*
 * MainWindow 头文件
 *
 * 功能：RK3568 开发板 LED 控制应用的主窗口类定义。
 * 通过 sysfs 接口（/sys/class/leds/work/brightness）
 * 控制 GPIO0_C0 引脚上的 work LED 的亮灭。
 */

#include <QMainWindow>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    /* 窗口显示事件，进入全屏模式 */
    void showEvent(QShowEvent *event) override;

private slots:
    /* 槽函数：打开 LED（写入亮度值 255） */
    void turnOnLed();

    /* 槽函数：关闭 LED（写入亮度值 0） */
    void turnOffLed();

    /* 槽函数：定时刷新 LED 状态显示 */
    void refreshStatus();

private:
    Ui::MainWindow *ui;          /* Qt Designer 生成的 UI 对象 */
    QPushButton *onButton;       /* 开灯按钮 */
    QPushButton *offButton;      /* 关灯按钮 */
    QLabel *statusLabel;         /* 显示当前状态（已开启/已关闭）的标签 */
    QLabel *brightnessLabel;     /* 显示当前亮度数值的标签 */
    QWidget *ledIndicator;       /* LED 圆形指示器（模拟灯的效果） */
    QTimer *timer;               /* 定时器，每秒刷新一次 LED 状态 */

    /* 写入亮度值到 sysfs 文件 */
    void setLedBrightness(int value);

    /* 从 sysfs 文件读取当前亮度值，失败返回 -1 */
    int getLedBrightness();

    /* 更新 LED 指示器的样式（亮/灭两种外观） */
    void updateLedStyle(bool on);

    /* sysfs 亮度控制文件的路径常量 */
    static const QString kLedBrightnessPath;
};

#endif // MAINWINDOW_H
