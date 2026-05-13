#include "rtsp_server.h"
#include <spdlog/spdlog.h>
#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QtEndian>
#include <cstring>

RtspServer::RtspServer(QObject *parent) : QObject(parent) {
    rtp_.ssrc = QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF;
}

RtspServer::~RtspServer() { stop(); }

bool RtspServer::start(int port, const char * /*streamName*/) {
    tcpServer_ = new QTcpServer(this);
    QObject::connect(tcpServer_, &QTcpServer::newConnection, this, &RtspServer::onNewConnection);
    if (!tcpServer_->listen(QHostAddress::Any, port)) {
        spdlog::error("RTSP: listen port {} failed", port);
        return false;
    }
    running_ = true;
    spdlog::info("RTSP server listening on port {}", port);
    return true;
}

void RtspServer::stop() {
    running_ = false;
    if (tcpServer_) { tcpServer_->close(); delete tcpServer_; tcpServer_ = nullptr; }
    for (auto *c : clients_) { c->close(); c->deleteLater(); }
    clients_.clear();
    recvBuf_.clear();
    rtp_.rtspSock = nullptr;
    hasClient_ = false;
}

void RtspServer::onNewConnection() {
    while (tcpServer_->hasPendingConnections()) {
        auto *sock = tcpServer_->nextPendingConnection();
        recvBuf_[sock] = QByteArray();
        QObject::connect(sock, &QTcpSocket::readyRead, this, &RtspServer::onReadyRead);
        QObject::connect(sock, &QTcpSocket::disconnected, [this, sock]() {
            clients_.removeAll(sock);
            recvBuf_.remove(sock);
            sock->deleteLater();
            if (clients_.isEmpty()) { hasClient_ = false; emit clientDisconnected(); }
        });
        clients_.append(sock);
        spdlog::info("RTSP: client {}", sock->peerAddress().toString().toStdString());
        emit clientConnected();
    }
}

void RtspServer::onReadyRead() {
    auto *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    QByteArray &buf = recvBuf_[sock];
    QByteArray data = sock->readAll();

    // 跳过 interleaved 帧（客户端 RTCP 包）
    for (int i = 0; i < data.size(); ) {
        if ((uint8_t)data[i] == '$' && i + 4 <= data.size()) {
            int frameLen = ((uint8_t)data[i+2] << 8) | (uint8_t)data[i+3];
            i += 4 + frameLen;  // 跳过整个 interleaved 帧
        } else {
            buf.append(data[i]);
            i++;
        }
    }

    if (buf.contains("\r\n\r\n"))
        handleRequest(sock);
}

void RtspServer::handleRequest(QTcpSocket *sock) {
    QByteArray &buf = recvBuf_[sock];

    while (buf.contains("\r\n\r\n")) {
        int endIdx = buf.indexOf("\r\n\r\n") + 4;
        QByteArray reqByte = buf.left(endIdx);
        buf.remove(0, endIdx);

        QString req = QString::fromUtf8(reqByte);
        spdlog::info("RTSP req: {}", req.left(250).toStdString());

        QStringList lines = req.split("\r\n");
        if (lines.isEmpty()) continue;

        QString first = lines[0];
        QString method = first.section(' ', 0, 0);
        QString url    = first.section(' ', 1, 1);
        QString cseq   = "1";
        for (const auto &l : lines) {
            if (l.startsWith("CSeq:", Qt::CaseInsensitive))
                cseq = l.section(':', 1).trimmed();
        }

        QByteArray resp;

        if (method == "OPTIONS") {
            resp = QString("RTSP/1.0 200 OK\r\nServer: rk3568\r\nCSeq: %1\r\nPublic: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY\r\n\r\n").arg(cseq).toUtf8();

        } else if (method == "DESCRIBE") {
            QString sdp = QString(
                "v=0\r\no=- %1 %1 IN IP4 %2\r\ns=H.264 Stream\r\nc=IN IP4 %2\r\nt=0 0\r\n"
                "a=control:*\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n"
                "a=control:track0\r\n"
                "a=fmtp:96 packetization-mode=1;"
                "sprop-parameter-sets=Z0LAKI2NUDwBEvLCAAADAAIAAAMAeR4RCNQ=,aM4xsg==\r\n")
                .arg(rtp_.ssrc).arg(sock->localAddress().toString());
            QByteArray sdpBa = sdp.toUtf8();
            resp = QString("RTSP/1.0 200 OK\r\nCSeq: %1\r\nContent-Base: %2\r\n"
                "Content-Type: application/sdp\r\nContent-Length: %3\r\n\r\n")
                .arg(cseq).arg(url).arg(sdpBa.size()).toUtf8() + sdpBa;

        } else if (method == "SETUP") {
            // 默认 TCP interleaved 模式（穿越防火墙）
            rtp_.rtspSock = sock;
            rtp_.tcpMode  = true;
            rtp_.seqNum = rtp_.timestamp = 0;
            hasClient_ = true;

            resp = QString("RTSP/1.0 200 OK\r\nCSeq: %1\r\nTransport: "
                "RTP/AVP/TCP;unicast;interleaved=0-1\r\n"
                "Session: 00000001\r\n\r\n").arg(cseq).toUtf8();
            spdlog::info("RTSP SETUP TCP interleaved mode");

        } else if (method == "PLAY") {
            resp = QString("RTSP/1.0 200 OK\r\nCSeq: %1\r\nSession: 00000001\r\n"
                "Range: npt=0.000-\r\n\r\n").arg(cseq).toUtf8();
            spdlog::info("RTSP PLAY");
            sock->write(resp);
            // 立即发送缓存的 SPS+PPS+IDR
            flushInitialNals();
            continue;

        } else if (method == "TEARDOWN") {
            resp = QString("RTSP/1.0 200 OK\r\nCSeq: %1\r\n\r\n").arg(cseq).toUtf8();
            hasClient_ = false;
            spdlog::info("RTSP TEARDOWN");

        } else {
            resp = QString("RTSP/1.0 501 Not Implemented\r\nCSeq: %1\r\n\r\n").arg(cseq).toUtf8();
        }

        int written = sock->write(resp);
        sock->flush();
        spdlog::debug("RTSP: wrote {} bytes for {}", written, method.toStdString());
    }
}

// RTP/UDP 发送部分不变（保持原样）
// ============================================================================
// NAL 解析辅助：遍历 annex B 格式中的所有 NAL 单元
// ============================================================================
template<typename F>
static void forEachNalu(const uint8_t *data, size_t len, F callback) {
    const uint8_t *end = data + len;
    const uint8_t *p = data;

    while (p + 3 < end) {
        // 找起始码 0x00000001 或 0x000001
        if (p[0] != 0 || p[1] != 0) { p++; continue; }
        int scLen;
        if (p[2] == 1) scLen = 3;
        else if (p[2] == 0 && p[3] == 1) scLen = 4;
        else { p++; continue; }

        const uint8_t *nalStart = p + scLen;
        if (nalStart >= end) break;

        uint8_t nalType = nalStart[0] & 0x1F;

        // 找下一个起始码
        const uint8_t *next = nalStart;
        while (next + 3 < end) {
            if (next[0] == 0 && next[1] == 0) {
                if (next[2] == 1 || (next[2] == 0 && next[3] == 1))
                    break;
            }
            next++;
        }

        callback(p, next - p, nalType);  // 回调：完整NAL含起始码

        p = next;
    }
}

void RtspServer::feedNALU(const uint8_t *data, size_t len, uint64_t /*pts*/) {
    if (!hasClient_ || !rtp_.rtspSock || len < 4) {
        cacheNalu(data, len);
        return;
    }

    bool firstNal = true;
    forEachNalu(data, len, [&](const uint8_t *nal, size_t nalLen, uint8_t type) {
        (void)type;
        sendRawNal(nal, nalLen);
        if (firstNal) {
            rtp_.timestamp += 3000;
            firstNal = false;
        }
    });
    cacheNalu(data, len);
}

void RtspServer::cacheNalu(const uint8_t *data, size_t len) {
    forEachNalu(data, len, [&](const uint8_t *nal, size_t nalLen, uint8_t type) {
        if (type == 7) {  // SPS
            cachedSpsPps_.clear();
            cachedSpsPps_.append((const char*)nal, nalLen);
        } else if (type == 8) {  // PPS
            cachedSpsPps_.append((const char*)nal, nalLen);
        } else if (type == 5) {  // IDR
            cachedIdr_ = QByteArray((const char*)nal, nalLen);
        }
    });
}

void RtspServer::flushInitialNals() {
    if (!rtp_.rtspSock) { spdlog::warn("flush: no rtsp socket"); return; }
    spdlog::info("flush: sps={} idr={}", cachedSpsPps_.size(), cachedIdr_.size());
    if (!cachedSpsPps_.isEmpty()) {
        sendRawNal((const uint8_t*)cachedSpsPps_.constData(), cachedSpsPps_.size());
    }
    if (!cachedIdr_.isEmpty()) {
        sendRawNal((const uint8_t*)cachedIdr_.constData(), cachedIdr_.size());
    }
}

void RtspServer::sendRawNal(const uint8_t *data, size_t len) {
    const uint8_t *nal = data;
    if (nal[0] == 0 && nal[1] == 0) {
        if (nal[2] == 1) nal += 3;
        else if (nal[2] == 0 && nal[3] == 1) nal += 4;
    }
    size_t nalLen = len - (nal - data);
    if (nalLen > 0) sendRtpPacket(nal, nalLen);
}

void RtspServer::sendRtpPacket(const uint8_t *nalData, size_t nalLen) {
    if (nalLen <= (size_t)(MTU_SIZE - 12)) {
        QByteArray pkt(12 + nalLen, 0);
        uint8_t *hdr = (uint8_t*)pkt.data();
        hdr[0] = 0x80; hdr[1] = 96 | 0x80;
        qToBigEndian(rtp_.seqNum, hdr + 2);
        qToBigEndian(rtp_.timestamp, hdr + 4);
        qToBigEndian(rtp_.ssrc, hdr + 8);
        memcpy(hdr + 12, nalData, nalLen);
        sendInterleaved(0, pkt);
    } else {
        rtpSendFuA(nalData, nalLen);
    }
    rtp_.seqNum++;
}

void RtspServer::rtpSendFuA(const uint8_t *nalData, size_t nalLen) {
    uint8_t fuIndicator = (nalData[0] & 0xE0) | 28;
    size_t offset = 1;
    while (offset < nalLen) {
        size_t chunk = qMin(nalLen - offset, (size_t)(MTU_SIZE - 14));
        QByteArray pkt(14 + chunk, 0);
        uint8_t *hdr = (uint8_t*)pkt.data();
        hdr[0] = 0x80; hdr[1] = 96;
        qToBigEndian(rtp_.seqNum, hdr + 2);
        qToBigEndian(rtp_.timestamp, hdr + 4);
        qToBigEndian(rtp_.ssrc, hdr + 8);
        hdr[12] = fuIndicator;
        bool first = (offset == 1), last = (offset + chunk >= nalLen);
        hdr[13] = (first ? 0x80 : 0) | (last ? 0x40 : 0) | (nalData[0] & 0x1F);
        if (last) hdr[1] |= 0x80;
        memcpy(hdr + 14, nalData + offset, chunk);
        sendInterleaved(0, pkt);
        offset += chunk;
        rtp_.seqNum++;
    }
}

void RtspServer::sendInterleaved(uint8_t channel, const QByteArray &rtpData) {
    if (!rtp_.rtspSock) return;
    QByteArray frame;
    frame.append('$');
    frame.append((char)channel);
    uint16_t len = rtpData.size();
    frame.append((char)(len >> 8));
    frame.append((char)(len & 0xFF));
    frame.append(rtpData);
    rtp_.rtspSock->write(frame);
}
