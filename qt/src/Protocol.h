#pragma once

#include <QJsonObject>
#include <QByteArray>

class QTcpSocket;

// Protocolo de transferencia (propio §6.2), canal TCP.
// Framing de control: [uint32 big-endian len][payload JSON].
// Datos de archivo: `size` bytes crudos justo tras el ITEM_HEADER.
namespace Proto {

constexpr quint16 kTcpPort = 51889;
constexpr quint16 kWebPort = 51890;   // servidor HTTP del "modo web"
constexpr int kVersion = 1;
constexpr qint64 kChunk = 1024 * 1024;     // 1 MB por trozo (RAM 1 MB, +10-15% en GbE)
constexpr int kIoTimeoutMs = 30000;        // timeout por operación bloqueante

// Tipos de mensaje de control (campo "type").
namespace Msg {
inline constexpr char Hello[]    = "hello";
inline constexpr char Accept[]   = "accept";
inline constexpr char Reject[]   = "reject";
inline constexpr char Item[]     = "item";
inline constexpr char Done[]     = "done";
inline constexpr char Complete[] = "complete";
}

// --- Operaciones bloqueantes sobre un QTcpSocket ya conectado ---

// Escribe un mensaje de control JSON con prefijo de longitud.
bool writeMessage(QTcpSocket *sock, const QJsonObject &obj);

// Lee un mensaje de control JSON. Devuelve false si hay error/timeout/cierre.
bool readMessage(QTcpSocket *sock, QJsonObject &out, int timeoutMs = kIoTimeoutMs);

// Escribe exactamente `len` bytes crudos (con back-pressure).
bool writeRaw(QTcpSocket *sock, const char *data, qint64 len);

// Lee exactamente `len` bytes crudos en `buf` (buffer >= len).
bool readExact(QTcpSocket *sock, char *buf, qint64 len, int timeoutMs = kIoTimeoutMs);

} // namespace Proto
