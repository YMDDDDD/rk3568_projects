#pragma once
#include "frame_ref.h"
#include "detector.h"
#include <QOpenGLWidget>
#include <QOpenGLShaderProgram>
#include <QMutex>
#include <QVector>

class VideoWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    void renderFrame(FrameRefPtr ref);
    void renderRawNV12(const uint8_t *data, int w, int h, int stride = 0);
    void renderDmaBuf(int dmabufFd, int w, int h, int stride);
    void setDetections(QVector<Detection> detections);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    void initShaders();
    void importDmaBuf(int dmabufFd);

    GLuint                 texY_     = 0;
    GLuint                 texUV_    = 0;
    GLuint                 vao_      = 0;
    GLuint                 vbo_      = 0;
    QOpenGLShaderProgram  *program_  = nullptr;

    bool                   hasFrame_ = false;
    int                    frameW_   = 0;
    int                    frameH_   = 0;
    std::vector<uint8_t>   compactBuf_;

    QMutex                 detMutex_;
    QVector<Detection>     detections_;
};
