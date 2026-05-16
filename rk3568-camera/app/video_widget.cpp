#include "video_widget.h"
#include <spdlog/spdlog.h>
#include <QPainter>
#include <QOpenGLFunctions>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm/drm_fourcc.h>
#include <unistd.h>

// glEGLImageTargetTexture2DOES: 通过函数指针动态加载，避免与 Qt 头中 GL 包含冲突
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum target, void *image);

static const char *vertexShaderSource = R"(
    attribute vec2 position;
    attribute vec2 texCoord;
    varying vec2 vTexCoord;
    void main() {
        gl_Position = vec4(position, 0.0, 1.0);
        vTexCoord = texCoord;
    }
)";

static const char *fragmentShaderSource = R"(
    precision mediump float;
    varying vec2 vTexCoord;
    uniform sampler2D texY;
    uniform sampler2D texUV;
    void main() {
        float y  = texture2D(texY,  vTexCoord).r;
        float u  = texture2D(texUV, vTexCoord).r - 0.5;
        float v  = texture2D(texUV, vTexCoord).g - 0.5;
        float r = y + 1.402 * v;
        float g = y - 0.344 * u - 0.714 * v;
        float b = y + 1.772 * u;
        gl_FragColor = vec4(r, g, b, 1.0);
    }
)";

static const GLfloat vertices[] = {
    -1.0f,  1.0f,  0.0f, 0.0f,
    -1.0f, -1.0f,  0.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
};

VideoWidget::VideoWidget(QWidget *parent) : QOpenGLWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
}

VideoWidget::~VideoWidget() {
    makeCurrent();
    if (texY_)  glDeleteTextures(1, &texY_);
    if (texUV_) glDeleteTextures(1, &texUV_);
    if (vbo_)   glDeleteBuffers(1, &vbo_);
    if (vao_)   glDeleteVertexArrays(1, &vao_);
    doneCurrent();
}

void VideoWidget::setDetections(QVector<Detection> detections) {
    QMutexLocker locker(&detMutex_);
    detections_ = std::move(detections);
}

void VideoWidget::initializeGL() {
    initShaders();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));

    glGenTextures(1, &texY_);
    glGenTextures(1, &texUV_);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    spdlog::info("VideoWidget OpenGL initialized");
}

void VideoWidget::initShaders() {
    program_ = new QOpenGLShaderProgram(this);
    program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    program_->link();
    program_->bind();
    program_->setUniformValue("texY", 0);
    program_->setUniformValue("texUV", 1);
}

void VideoWidget::renderFrame(FrameRefPtr ref) {
    if (!ref) return;
    renderDmaBuf(ref->dmabufFd, ref->width, ref->height, ref->stride);
}

void VideoWidget::renderRawNV12(const uint8_t *data, int w, int h, int stride) {
    if (w <= 0 || h <= 0 || !data) return;

    frameW_   = w;
    frameH_   = h;
    hasFrame_ = true;

    makeCurrent();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h,
                 0, GL_RED, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // UV 正确偏移: stride × h（非 w × h！）
    const uint8_t *uv = data + (stride * h);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texUV_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, w / 2, h / 2,
                 0, GL_RG, GL_UNSIGNED_BYTE, uv);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    doneCurrent();
    update();
}

void VideoWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (!hasFrame_) return;

    program_->bind();
    glBindVertexArray(vao_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texUV_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    program_->release();
}

void VideoWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void VideoWidget::renderDmaBuf(int dmabufFd, int w, int h, int stride) {
    if (dmabufFd < 0 || w <= 0 || h <= 0) return;

    frameW_   = w;
    frameH_   = h;
    hasFrame_ = true;

    makeCurrent();

    // 动态加载 glEGLImageTargetTexture2DOES
    static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEglImageTarget =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    EGLDisplay dpy = eglGetCurrentDisplay();
    if (dpy == EGL_NO_DISPLAY || !glEglImageTarget) { doneCurrent(); return; }

    EGLint attr_y[] = {
        EGL_WIDTH,                     w,
        EGL_HEIGHT,                    h,
        EGL_LINUX_DRM_FOURCC_EXT,      (EGLint)DRM_FORMAT_R8,
        EGL_DMA_BUF_PLANE0_FD_EXT,     dmabufFd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  stride,
        EGL_NONE
    };
    EGLImageKHR imgY = eglCreateImageKHR(dpy, EGL_NO_CONTEXT,
                                          EGL_LINUX_DMA_BUF_EXT, nullptr, attr_y);
    if (imgY != EGL_NO_IMAGE_KHR) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texY_);
        glEglImageTarget(GL_TEXTURE_2D, imgY);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        eglDestroyImageKHR(dpy, imgY);
    }

    EGLint attr_uv[] = {
        EGL_WIDTH,                     w / 2,
        EGL_HEIGHT,                    h / 2,
        EGL_LINUX_DRM_FOURCC_EXT,      (EGLint)DRM_FORMAT_GR88,
        EGL_DMA_BUF_PLANE0_FD_EXT,     dmabufFd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)(stride * h),
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  stride,
        EGL_NONE
    };
    EGLImageKHR imgUV = eglCreateImageKHR(dpy, EGL_NO_CONTEXT,
                                           EGL_LINUX_DMA_BUF_EXT, nullptr, attr_uv);
    if (imgUV != EGL_NO_IMAGE_KHR) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texUV_);
        glEglImageTarget(GL_TEXTURE_2D, imgUV);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        eglDestroyImageKHR(dpy, imgUV);
    }

    doneCurrent();
    update();
}

void VideoWidget::importDmaBuf(int) {
    // dmabuf 导入已由 renderDmaBuf() 完成，此函数保留兼容
}
