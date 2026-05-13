#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QList>
#include <QByteArray>
#include <cstdint>
#include <functional>

// ============================================================================
// 极简内嵌 RTSP 服务器：QTcpServer + RTP/UDP
// 支持 VLC 拉流 rtsp://IP:8554/live
// ============================================================================

struct RtpSession {
    QTcpSocket   *rtspSock = nullptr;   // 用于 TCP interleaved
    bool          tcpMode  = false;
    uint16_t      seqNum   = 0;
    uint32_t      ssrc     = 0;
    uint32_t      timestamp = 0;
};

class RtspServer : public QObject {
    Q_OBJECT
public:
    explicit RtspServer(QObject *parent = nullptr);
    ~RtspServer() override;

    bool start(int port = 8554, const char *streamName = "/live");
    void stop();

    // MPP 编码回调：喂 H.264 NAL 单元
    void feedNALU(const uint8_t *data, size_t len, uint64_t pts);
    void flushInitialNals();  // PLAY 后发送缓存的 SPS/PPS/IDR

    bool isRunning() const { return running_; }

signals:
    void clientConnected();
    void clientDisconnected();

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    void handleRequest(QTcpSocket *sock);
    void sendInterleaved(uint8_t channel, const QByteArray &rtpData);
    void sendRtpPacket(const uint8_t *nalData, size_t nalLen);
    void sendRawNal(const uint8_t *data, size_t len);
    void cacheNalu(const uint8_t *data, size_t len);
    void rtpSendFuA(const uint8_t *nalData, size_t nalLen);

    QTcpServer      *tcpServer_ = nullptr;
    QMap<QTcpSocket*, QByteArray> recvBuf_;
    QList<QTcpSocket*> clients_;
    RtpSession       rtp_;
    bool             running_  = false;
    bool             hasClient_ = false;

    // NAL 缓冲：缓存 SPS/PPS/最近 IDR 供新客户端
    QByteArray       cachedSpsPps_;
    QByteArray       cachedIdr_;
    uint64_t         cachedPts_ = 0;

    static constexpr uint32_t RTP_CLOCK = 90000;
    static constexpr int      MTU_SIZE  = 1400;
};
