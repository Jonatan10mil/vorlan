#include "Protocol.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QtEndian>

namespace Proto {

bool writeMessage(QTcpSocket *sock, const QJsonObject &obj)
{
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    quint32 len = qToBigEndian<quint32>(static_cast<quint32>(payload.size()));
    if (!writeRaw(sock, reinterpret_cast<const char *>(&len), sizeof(len)))
        return false;
    return writeRaw(sock, payload.constData(), payload.size());
}

bool readMessage(QTcpSocket *sock, QJsonObject &out, int timeoutMs)
{
    quint32 lenBE = 0;
    if (!readExact(sock, reinterpret_cast<char *>(&lenBE), sizeof(lenBE), timeoutMs))
        return false;
    const quint32 len = qFromBigEndian<quint32>(lenBE);
    if (len == 0 || len > 8 * 1024 * 1024)   // sanidad: control <= 8 MB
        return false;
    QByteArray payload(static_cast<int>(len), Qt::Uninitialized);
    if (!readExact(sock, payload.data(), len, timeoutMs))
        return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    out = doc.object();
    return true;
}

bool writeRaw(QTcpSocket *sock, const char *data, qint64 len)
{
    qint64 sent = 0;
    while (sent < len) {
        const qint64 n = sock->write(data + sent, len - sent);
        if (n < 0)
            return false;
        sent += n;
        // Back-pressure: no dejar crecer el buffer sin límite.
        while (sock->bytesToWrite() > 4 * Proto::kChunk) {
            if (!sock->waitForBytesWritten(kIoTimeoutMs))
                return false;
        }
    }
    while (sock->bytesToWrite() > 0) {
        if (!sock->waitForBytesWritten(kIoTimeoutMs))
            return false;
    }
    return true;
}

bool readExact(QTcpSocket *sock, char *buf, qint64 len, int timeoutMs)
{
    qint64 got = 0;
    while (got < len) {
        if (sock->bytesAvailable() == 0) {
            if (!sock->waitForReadyRead(timeoutMs))
                return false;
        }
        const qint64 n = sock->read(buf + got, len - got);
        if (n < 0)
            return false;
        got += n;
    }
    return true;
}

} // namespace Proto
