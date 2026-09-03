#include "Discovery.h"
#include <QDir>
#if defined(Q_OS_ANDROID)
#include "AndroidDevice.h"
#elif defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#endif
#include "AndroidMulticast.h"

#include <QUdpSocket>
#include <QTimer>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QSysInfo>
#include <QHostInfo>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QRegularExpression>

// Tipo de este dispositivo, para mostrar el icono correcto (PC, portátil, móvil, TV).
QString detectDeviceType()
{
#if defined(Q_OS_ANDROID)
    return AndroidDevice::isTelevision() ? QStringLiteral("tv")
                                         : QStringLiteral("phone");
#elif defined(Q_OS_IOS)
    return QStringLiteral("phone");
#elif defined(Q_OS_LINUX)
    // Con batería = portátil (los equipos de escritorio no tienen BAT*).
    const QDir psu(QStringLiteral("/sys/class/power_supply"));
    const QStringList bats = psu.entryList({QStringLiteral("BAT*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    return bats.isEmpty() ? QStringLiteral("desktop") : QStringLiteral("laptop");
#elif defined(Q_OS_WIN)
    SYSTEM_POWER_STATUS st{};
    if (GetSystemPowerStatus(&st) && st.BatteryFlag != 128 && st.BatteryFlag != 255)
        return QStringLiteral("laptop");     // 128 = sin batería del sistema
    return QStringLiteral("desktop");
#elif defined(Q_OS_MACOS)
    char model[128]; size_t len = sizeof(model);
    if (sysctlbyname("hw.model", model, &len, nullptr, 0) == 0
        && QString::fromLatin1(model).contains(QStringLiteral("Book")))
        return QStringLiteral("laptop");     // MacBook / MacBook Pro / Air
    return QStringLiteral("desktop");
#else
    return QStringLiteral("desktop");
#endif
}

Discovery::Discovery(QString selfId, QString selfName, QObject *parent)
    : QObject(parent)
    , m_selfId(std::move(selfId))
    , m_selfName(std::move(selfName))
{
    if (m_selfName.isEmpty())
        m_selfName = QStringLiteral("VorLAN");

#if defined(Q_OS_ANDROID)
    m_selfPlatform = QStringLiteral("android");
#elif defined(Q_OS_IOS)
    m_selfPlatform = QStringLiteral("ios");
#elif defined(Q_OS_MACOS)
    m_selfPlatform = QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    m_selfPlatform = QStringLiteral("windows");
#else
    m_selfPlatform = QStringLiteral("linux");
#endif
    m_selfType = detectDeviceType();
}

Discovery::~Discovery()
{
    stop();
}

bool Discovery::start()
{
    if (m_socket)
        return true;

    // Android: sin MulticastLock no se reciben los broadcasts UDP.
    AndroidMulticast::acquire();

    m_socket = new QUdpSocket(this);
    // ShareAddress + ReuseAddressHint: varias instancias en la misma máquina
    // pueden escuchar el mismo puerto (útil para probar el duelo localmente).
    if (!m_socket->bind(QHostAddress::AnyIPv4, kDiscoveryPort,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "[Discovery] no se pudo hacer bind al puerto"
                   << kDiscoveryPort << ":" << m_socket->errorString();
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &Discovery::onReadyRead);

    m_announceTimer = new QTimer(this);
    m_announceTimer->setInterval(kAnnounceIntervalMs);
    connect(m_announceTimer, &QTimer::timeout, this, &Discovery::sendAnnounce);
    m_announceTimer->start();

    m_expiryTimer = new QTimer(this);
    m_expiryTimer->setInterval(kPeerTimeoutMs / 3); // chequeo ~5s (1/3 del timeout)
    connect(m_expiryTimer, &QTimer::timeout, this, &Discovery::checkExpired);
    m_expiryTimer->start();

    // Al arrancar: anuncio + probe para que otros respondan cuanto antes.
    sendPacket(QStringLiteral("announce"));
    sendPacket(QStringLiteral("probe"));

    qInfo() << "[Discovery] escuchando en UDP" << kDiscoveryPort
            << "id" << m_selfId << "name" << m_selfName;
    return true;
}

void Discovery::stop()
{
    if (!m_socket)
        return;
    sendPacket(QStringLiteral("bye"));   // salida limpia
    m_announceTimer->stop();
    m_expiryTimer->stop();
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
    AndroidMulticast::release();
}

void Discovery::sendAnnounce()
{
    if (!m_visible)
        return;   // modo invisible: no anunciarse
    sendPacket(QStringLiteral("announce"));
    // Difundir el avatar de vez en cuando (cada ~3 anuncios) como respaldo del unicast.
    if (!m_selfAvatarThumb.isEmpty() && (++m_announceCount % 3) == 0)
        broadcastAvatar();
}

void Discovery::setVisible(bool v)
{
    if (v == m_visible)
        return;
    m_visible = v;
    if (!m_socket)
        return;
    if (v) {
        // Volver a ser visible: reanudar anuncios y provocar el rendez-vous.
        if (m_announceTimer)
            m_announceTimer->start();
        sendPacket(QStringLiteral("announce"));
        sendPacket(QStringLiteral("probe"));
    } else {
        // Pasar a invisible: dejar de anunciar y pedir que nos quiten ya.
        if (m_announceTimer)
            m_announceTimer->stop();
        sendPacket(QStringLiteral("bye"));
    }
}

void Discovery::setSelfName(const QString &name)
{
    const QString n = name.trimmed().isEmpty() ? QStringLiteral("VorLAN") : name.trimmed();
    if (n == m_selfName)
        return;
    m_selfName = n;
    if (m_socket)
        sendPacket(QStringLiteral("announce"));   // anunciar el nombre nuevo
}

void Discovery::setSelfAvatarThumb(const QString &b64)
{
    if (b64 == m_selfAvatarThumb)
        return;
    m_selfAvatarThumb = b64;
    // Difundir la miniatura nueva (broadcast + unicast a los peers conocidos).
    if (m_socket && !m_selfAvatarThumb.isEmpty()) {
        broadcastAvatar();
        for (const Peer &p : std::as_const(m_peers))
            sendAvatarTo(p.address);
    }
}

// Envía nuestra miniatura de avatar (paquete propio, separado del announce para no
// engordarlo: si se fragmenta/pierde solo falta el avatar, el descubrimiento sigue).
void Discovery::sendAvatarTo(const QHostAddress &addr)
{
    if (!m_socket || m_selfAvatarThumb.isEmpty() || addr.isNull())
        return;
    const QJsonObject obj{
        {QStringLiteral("v"), kProtocolVersion},
        {QStringLiteral("id"), m_selfId},
        {QStringLiteral("type"), QStringLiteral("avatar")},
        {QStringLiteral("ava"), m_selfAvatarThumb},
    };
    m_socket->writeDatagram(QJsonDocument(obj).toJson(QJsonDocument::Compact),
                            addr, kDiscoveryPort);
}

// Guarda la miniatura recibida en caché y devuelve su ruta local ("" si falla). LRU: poda a 50 archivos más recientes.
QString Discovery::cacheAvatar(const QString &id, const QByteArray &jpeg)
{
    if (jpeg.isEmpty())
        return QString();
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    dir += QStringLiteral("/peer-avatars");
    QDir().mkpath(dir);
    QString safe = id;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    const QString path = dir + QLatin1Char('/') + safe + QStringLiteral(".jpg");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(jpeg);
    f.close();
    // LRU: mantener máx 50 avatares, borrar los más antiguos (funciona en Linux/Win/macOS/Android)
    QDir d(dir);
    QFileInfoList files = d.entryInfoList(QDir::Files, QDir::Time);
    for (int i = 50; i < files.size(); ++i) QFile::remove(files[i].absoluteFilePath());
    return QUrl::fromLocalFile(path).toString();
}

void Discovery::sendPacket(const QString &type)
{
    if (!m_socket)
        return;

    QJsonObject obj{
        {QStringLiteral("v"), kProtocolVersion},
        {QStringLiteral("id"), m_selfId},
        {QStringLiteral("name"), m_selfName},
        {QStringLiteral("platform"), m_selfPlatform},
        {QStringLiteral("dtype"), m_selfType},
        {QStringLiteral("port"), kTcpPort},
        {QStringLiteral("type"), type},
    };
    sendDatagram(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// Broadcast global + broadcast dirigido por cada interfaz IPv4 activa
// (algunas redes/enrutadores filtran 255.255.255.255).
void Discovery::sendDatagram(const QByteArray &data)
{
    if (!m_socket)
        return;
    // 1) Broadcast limitado (255.255.255.255).
    m_socket->writeDatagram(data, QHostAddress::Broadcast, kDiscoveryPort);

    // 2) Broadcast DIRIGIDO por interfaz (p.ej. 192.168.100.255). En Android Qt
    //    a veces no rellena entry.broadcast(), así que lo calculamos con la máscara
    //    (ip | host-bits). Muchos APs descartan el 255.255.255.255 de clientes Wi-Fi
    //    pero sí reenvían el dirigido → imprescindible para que nos vean.
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            QHostAddress bcast = entry.broadcast();
            if (bcast.isNull()) {
                const int prefix = entry.prefixLength();
                if (prefix > 0 && prefix < 32) {
                    const quint32 ip = entry.ip().toIPv4Address();
                    const quint32 host = (1u << (32 - prefix)) - 1;
                    bcast = QHostAddress(ip | host);
                }
            }
            if (!bcast.isNull())
                m_socket->writeDatagram(data, bcast, kDiscoveryPort);
        }
    }

    // 3) Unicast a los peers ya conocidos (a prueba de aislamiento/descarte de
    //    broadcast del AP): si ya vimos a alguien, le llegamos directo.
    for (const Peer &p : std::as_const(m_peers)) {
        if (!p.address.isNull())
            m_socket->writeDatagram(data, p.address, kDiscoveryPort);
    }
}

// Difunde nuestra miniatura de avatar por broadcast (respaldo del unicast dirigido).
void Discovery::broadcastAvatar()
{
    if (!m_socket || m_selfAvatarThumb.isEmpty())
        return;
    const QJsonObject obj{
        {QStringLiteral("v"), kProtocolVersion},
        {QStringLiteral("id"), m_selfId},
        {QStringLiteral("type"), QStringLiteral("avatar")},
        {QStringLiteral("ava"), m_selfAvatarThumb},
    };
    sendDatagram(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void Discovery::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_socket->receiveDatagram();
        handlePacket(dg.data(), dg.senderAddress());
    }
}

void Discovery::handlePacket(const QByteArray &data, const QHostAddress &from)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject obj = doc.object();

    if (obj.value(QStringLiteral("v")).toInt() != kProtocolVersion)
        return;   // versión incompatible: ignorar (Fase 4 negociará)

    const QString id = obj.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id == m_selfId)
        return;   // ignorar nuestros propios paquetes

    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("bye")) {
        if (m_peers.remove(id) > 0)
            emit peerLost(id);
        return;
    }

    // Paquete de avatar: guardar la miniatura y refrescar el peer (si ya existe).
    if (type == QLatin1String("avatar")) {
        const QByteArray jpeg = QByteArray::fromBase64(
            obj.value(QStringLiteral("ava")).toString().toLatin1());
        const QString path = cacheAvatar(id, jpeg);
        if (path.isEmpty())
            return;
        m_avatarPaths.insert(id, path);
        auto it = m_peers.find(id);
        if (it != m_peers.end()) {
            it->avatarThumb = path;
            emit peerUpdated(*it);
        }
        return;
    }

    // announce / probe: registrar o refrescar el peer.
    Peer peer;
    peer.id = id;
    peer.name = obj.value(QStringLiteral("name")).toString();
    peer.platform = obj.value(QStringLiteral("platform")).toString();
    peer.dtype = obj.value(QStringLiteral("dtype")).toString();
    peer.avatarThumb = m_avatarPaths.value(id);   // conservar miniatura ya recibida
    peer.tcpPort = static_cast<quint16>(obj.value(QStringLiteral("port")).toInt());
    // Normaliza IPv4 mapeada en IPv6 (::ffff:a.b.c.d)
    bool ok = false;
    QHostAddress v4(from.toIPv4Address(&ok));
    peer.address = ok ? v4 : from;
    peer.lastSeen = QDateTime::currentDateTimeUtc();

    const bool isNew = !m_peers.contains(id);
    if (!isNew) {
        // Un mismo peer puede anunciarse desde varias IPs (broadcast dirigido por
        // cada interfaz). Preferimos la IP que está en la misma subred que nosotros
        // (alcanzable), no una de una interfaz virtual (docker/lxc/vbox).
        const QHostAddress oldAddr = m_peers.value(id).address;
        if (peer.address != oldAddr &&
            !addressReachable(peer.address) && addressReachable(oldAddr)) {
            peer.address = oldAddr;
        }
    }
    m_peers.insert(id, peer);

    if (isNew) {
        emit peerFound(peer);
        // Respondemos a un probe con un announce para acelerar el rendez-vous
        // (salvo en modo invisible: no debemos revelarnos).
        if (type == QLatin1String("probe") && m_visible)
            sendPacket(QStringLiteral("announce"));
        // Enviarle nuestra miniatura de avatar (unicast, al descubrirnos).
        sendAvatarTo(peer.address);
    } else {
        emit peerUpdated(peer);
    }
}

bool Discovery::addressReachable(const QHostAddress &addr)
{
    if (addr.isNull())
        return false;
    const quint32 a = addr.toIPv4Address();
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const int prefix = entry.prefixLength();
            if (prefix <= 0 || prefix > 32)
                continue;
            const quint32 local = entry.ip().toIPv4Address();
            const quint32 mask = (prefix == 32) ? 0xffffffffu
                                                 : ~((1u << (32 - prefix)) - 1);
            if ((local & mask) == (a & mask))
                return true;
        }
    }
    return false;
}

void Discovery::checkExpired()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList dead;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        if (it.value().lastSeen.msecsTo(now) > kPeerTimeoutMs)
            dead << it.key();
    }
    for (const QString &id : dead) {
        m_peers.remove(id);
        emit peerLost(id);
    }
}
