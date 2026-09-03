#pragma once

#include <QObject>
#include <QHash>
#include <QHostAddress>
#include <QDateTime>

class QUdpSocket;
class QTimer;

// Tipo de ESTE dispositivo: desktop|laptop|phone|tv (para el icono).
QString detectDeviceType();

// Un peer descubierto en la LAN.
struct Peer {
    QString id;         // uuid estable del dispositivo (identidad, no la IP)
    QString name;       // nombre visible
    QString platform;   // android|windows|linux|ios|macos
    QString dtype;      // tipo: desktop|laptop|phone|tablet|tv
    QString avatarThumb; // file:// URL de la miniatura recibida del peer (opcional)
    QHostAddress address;
    quint16 tcpPort = 0;
    QDateTime lastSeen;
};

// Descubrimiento LAN por UDP (protocolo propio §6.1).
// - Anuncia la presencia por broadcast cada `announceInterval`.
// - Escucha anuncios de otros y mantiene la lista de peers vivos.
// - Expira peers no vistos en `peerTimeout`.
class Discovery : public QObject
{
    Q_OBJECT
public:
    // La identidad (id estable + nombre) se pasa desde fuera (persistida).
    explicit Discovery(QString selfId, QString selfName,
                       QObject *parent = nullptr);
    ~Discovery() override;

    // Arranca socket, envía el primer anuncio y un probe.
    bool start();
    void stop();

    QString selfId() const { return m_selfId; }
    QString selfName() const { return m_selfName; }

    // Visibilidad: si es false, dejamos de anunciarnos y de responder a probes
    // (seguimos viendo a los demás y pudiendo enviarles). Al pasar a invisible
    // mandamos un "bye" para que nos quiten de sus listas al instante.
    void setVisible(bool v);

    // Cambiar el nombre/avatar en caliente (reenvía un anuncio).
    void setSelfName(const QString &name);
    void setSelfAvatarThumb(const QString &b64);   // miniatura JPEG en base64 para difundir

signals:
    void peerFound(const Peer &peer);
    void peerUpdated(const Peer &peer);
    void peerLost(const QString &id);

private slots:
    void onReadyRead();
    void sendAnnounce();       // anuncio periódico
    void checkExpired();       // purga peers muertos

private:
    void sendPacket(const QString &type);
    void sendDatagram(const QByteArray &data);     // broadcast global + dirigido
    void sendAvatarTo(const QHostAddress &addr);   // envía nuestra miniatura (unicast)
    void broadcastAvatar();                        // difunde la miniatura por broadcast
    void handlePacket(const QByteArray &data, const QHostAddress &from);
    QString cacheAvatar(const QString &id, const QByteArray &jpeg);   // guarda y devuelve la ruta
    static bool addressReachable(const QHostAddress &addr);   // misma subred local

    static constexpr quint16 kDiscoveryPort = 51888;
    static constexpr quint16 kTcpPort = 51889;      // se usará en Fase 2
    static constexpr int kAnnounceIntervalMs = 3000;
    static constexpr int kPeerTimeoutMs = 15000;
    static constexpr int kProtocolVersion = 1;

    QUdpSocket *m_socket = nullptr;
    QTimer *m_announceTimer = nullptr;
    QTimer *m_expiryTimer = nullptr;

    QString m_selfId;
    QString m_selfName;
    QString m_selfAvatarThumb;        // nuestra miniatura JPEG en base64 ("" si no hay)
    QString m_selfPlatform;
    QString m_selfType;      // tipo de este dispositivo (desktop|laptop|phone|tv)

    QHash<QString, Peer> m_peers;         // id -> peer
    QHash<QString, QString> m_avatarPaths; // id -> ruta de la miniatura cacheada
    int m_announceCount = 0;               // para difundir el avatar cada N anuncios
    bool m_visible = true;                 // ¿nos anunciamos a los demás?
};
