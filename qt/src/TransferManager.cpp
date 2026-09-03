#include "TransferManager.h"
#include "ITransport.h"
#include "Worker.h"
#include "AndroidDevice.h"
#include "AndroidNotify.h"
#include "SendQueueModel.h"
#include "AndroidStorage.h"
#include "FileActions.h"
#include "Discovery.h"
#include "WebServer.h"
#include "QrCode.h"
#include "Protocol.h"

#include <QLocale>
#include <QGuiApplication>
#include <QStyleHints>
#include <QClipboard>
#include <QImage>
#include <QDateTime>
#include <QUrl>
#include <QBuffer>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QHostInfo>
#include <QSysInfo>
#include <QSslSocket>
#include <QTimer>
#include <QFileInfo>
#include <QSettings>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

#if defined(Q_OS_WIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <sddl.h>          // ConvertSidToStringSidW

// SID del usuario actual ("S-1-5-21-…"), que es como Windows nombra la carpeta
// donde guarda la foto de la cuenta.
static QString currentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return QString();
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);   // 1a llamada: pedir tamaño
    QString sid;
    if (len > 0) {
        QByteArray buf(int(len), Qt::Uninitialized);
        if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
            LPWSTR str = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buf.data())->User.Sid, &str)) {
                sid = QString::fromWCharArray(str);
                LocalFree(str);
            }
        }
    }
    CloseHandle(token);
    return sid;
}
#endif

// Busca la foto del usuario del sistema operativo (para el avatar por defecto).
static QString findSystemUserPhoto()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    const QString home = QDir::homePath();
    QStringList candidates = { home + "/.face", home + "/.face.icon" };
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = qEnvironmentVariable("LOGNAME");
    if (!user.isEmpty())
        candidates << QStringLiteral("/var/lib/AccountsService/icons/") + user;
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return QUrl::fromLocalFile(c).toString();
#endif

#if defined(Q_OS_WIN)
    // Windows 8+ guarda la foto de la cuenta (la misma que se ve en el menú de
    // Inicio) en varias resoluciones dentro de:
    //   %PUBLIC%\AccountPictures\<SID>\{GUID}-Image<px>.jpg
    // Nos quedamos con la de más resolución. Si el usuario nunca puso una foto,
    // la carpeta no existe y el avatar se queda con el icono del SO (el
    // muñeco genérico de Windows no aporta nada).
    const QString sid = currentUserSid();
    if (!sid.isEmpty()) {
        const QString pub = QDir::fromNativeSeparators(
            qEnvironmentVariable("PUBLIC", QStringLiteral("C:/Users/Public")));
        QDir dir(pub + QStringLiteral("/AccountPictures/") + sid);
        if (dir.exists()) {
            static const QRegularExpression re(QStringLiteral("-Image(\\d+)\\.(?:jpe?g|png)$"),
                                               QRegularExpression::CaseInsensitiveOption);
            QString best;
            int bestPx = -1;
            // Ojo: Windows marca estas imágenes como ocultas + de sistema, así que
            // hay que pedirlas explícitamente (QDir::Files a secas no las ve).
            const auto entries = dir.entryInfoList({ QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                                     QStringLiteral("*.png") },
                                                   QDir::Files | QDir::Hidden | QDir::System);
            for (const QFileInfo &fi : entries) {
                const auto m = re.match(fi.fileName());
                const int px = m.hasMatch() ? m.captured(1).toInt() : 0;
                if (px > bestPx) { bestPx = px; best = fi.absoluteFilePath(); }
            }
            if (!best.isEmpty())
                return QUrl::fromLocalFile(best).toString();
        }
    }
    // Respaldo: en algunas instalaciones la ruta queda anotada en el registro.
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\AccountPicture"),
                  QSettings::NativeFormat);
    for (const auto &key : { "Image1080", "Image448", "Image240", "Image192", "Image96" }) {
        const QString p = reg.value(QLatin1String(key)).toString();
        if (!p.isEmpty() && QFileInfo::exists(p))
            return QUrl::fromLocalFile(p).toString();
    }
#endif
    return QString();
}

// ¿Interfaz virtual (VirtualBox, VPN, Docker, WSL…)? Tienen IP pero casi nunca
// son alcanzables desde el móvil que va a escanear el QR.
static bool isVirtualIface(const QNetworkInterface &iface)
{
    // Linux/macOS: el nombre del dispositivo ya lo delata.
    const QString n = iface.name().toLower();
    if (n.startsWith("docker") || n.startsWith("veth")  || n.startsWith("virbr")
     || n.startsWith("vbox")   || n.startsWith("tun")   || n.startsWith("tap")
     || n.startsWith("br-")    || n.startsWith("zt")    || n.startsWith("lxc")
     || n.startsWith("wg")     || n.startsWith("vmnet") || n.startsWith("ham")
     || n.startsWith("utun")   || n.startsWith("bridge")
     || n.startsWith("p2p")    || n.startsWith("dummy"))   // Android: Wi-Fi Direct, dummy
        return true;
    // En Windows name() es el GUID del adaptador, así que ninguno de los
    // prefijos de arriba encaja nunca: hay que mirar el nombre legible, que es
    // el de la conexión ("VirtualBox Host-Only Network", "vEthernet (WSL)"…).
    const QString h = iface.humanReadableName().toLower();
    static const char *marks[] = {
        "virtualbox", "vmware", "hyper-v", "vethernet", "docker", "wsl",
        "openvpn", "tap-windows", "wintun", "wireguard", "tailscale",
        "zerotier", "bluetooth", "wi-fi direct", "loopback"
    };
    for (const char *m : marks)
        if (h.contains(QLatin1String(m)))
            return true;
    return iface.type() == QNetworkInterface::Virtual;
}

// Interfaz de datos móviles (Android). Tiene IP e incluso puede ser la ruta por
// defecto, pero por ahí no hay ninguna LAN a la que llegue el otro dispositivo.
// Caso típico: móvil compartiendo internet — la ruta por defecto sale por rmnet
// mientras la LAN de verdad es la del punto de acceso (ap0/swlan0, 192.168.43.x).
// Sin esta comprobación, el criterio de ruta pondría delante la IP del operador.
static bool isCellularIface(const QNetworkInterface &iface)
{
    const QString n = iface.name().toLower();
    return n.startsWith("rmnet") || n.startsWith("ccmni")
        || n.startsWith("pdp_ip") || n.startsWith("wwan");
}

// Dirección de origen que el sistema usaría para salir del equipo, es decir la
// interfaz realmente en uso (Wi-Fi o cable). "Conectar" un socket UDP no hace
// handshake ni envía un solo paquete: únicamente resuelve la tabla de rutas.
// Es fiable donde adivinar por el nombre del adaptador falla.
// 192.0.2.1 es TEST-NET-1 (RFC 5737), reservada para documentación: nunca
// existe en una red real, solo sirve para que el SO elija la ruta por defecto.
static QHostAddress routedLocalAddress()
{
    QUdpSocket sock;
    sock.connectToHost(QHostAddress(QStringLiteral("192.0.2.1")), 53, QIODevice::ReadOnly);
    if (sock.state() != QAbstractSocket::ConnectedState && !sock.waitForConnected(50))
        return {};   // sin ruta por defecto (LAN sin salida): se usa la heurística
    const QHostAddress a = sock.localAddress();
    return (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
               ? a : QHostAddress();
}

// Cuanto MENOR es el número, mejor candidata es la dirección para que otro
// dispositivo se conecte.
static int addrRank(const QNetworkInterface &iface, const QHostAddress &ip,
                    const QHostAddress &routed)
{
    const QString s = ip.toString();
    if (s.startsWith(QLatin1String("169.254.")))                      return 90;  // link-local
    // El descarte de virtuales va ANTES del criterio de ruta: si hay una VPN
    // levantada se queda con la ruta por defecto, y para una app de LAN eso es
    // justo lo que no queremos ofrecer.
    if (isVirtualIface(iface))                                        return 70;
    if (isCellularIface(iface))                                       return 60;  // datos móviles
    if (!routed.isNull() && ip == routed)                             return 0;   // la de verdad
    if (s.startsWith(QLatin1String("192.168.")))                      return 10;  // LAN doméstica
    if (s.startsWith(QLatin1String("10.")))                           return 20;
    if (ip.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)) return 30;
    return 50;
}

TransferManager::TransferManager(QString selfId, QString selfName, QObject *parent)
    : QObject(parent)
    , m_deviceName(selfName)
{
    m_autoAccept = m_settings.autoAccept();
    m_downloadDir = m_settings.downloadDir();
    m_themeMode = m_settings.themeMode();
    m_language = m_settings.language();
    m_accentColor = m_settings.accentColor();
    m_webPin = m_settings.webPin();
    m_webPinEnabled = m_settings.webPinEnabled() && !m_webPin.isEmpty();
    m_webTls = m_settings.webTls();
    m_showFileNames = m_settings.showFileNames();
    m_closeToTray = m_settings.closeToTray();
    m_notificationsEnabled = m_settings.notificationsEnabled();
    m_discoverable = m_settings.discoverable();
    m_encrypt = m_settings.encrypt();
    m_avatarImage = m_settings.avatarImage();
    m_defaultAvatarImage = findSystemUserPhoto();   // foto del usuario del SO
    // Limpieza migracion: borrar claves legacy de versiones de prueba (no afectan si no existen)
    { QSettings s; s.remove("avatar"); s.remove("folderPrompted"); }

    m_queue = new SendQueueModel(this);

    m_transport = new Worker(std::move(selfId), std::move(selfName));
    m_transport->setAutoAccept(m_autoAccept);
    m_transport->setEncrypt(m_encrypt);
    m_transport->setShowNames(m_showFileNames);
    m_transport->setDownloadDir(m_downloadDir);
    m_transport->moveToThread(&m_thread);

    connect(&m_thread, &QThread::started, m_transport, &ITransport::startServer);
    connect(&m_thread, &QThread::finished, m_transport, &QObject::deleteLater);

    connect(this, &TransferManager::requestSendItems, m_transport, &ITransport::sendItems);

    connect(m_transport, &ITransport::progress, this, &TransferManager::onProgress);
    connect(m_transport, &ITransport::savingFile, this, &TransferManager::onSavingFile);
    connect(m_transport, &ITransport::receivedFolder, this, &TransferManager::onReceivedFolder);
    connect(m_transport, &ITransport::senderIdentified, this, &TransferManager::onSenderIdentified);
    connect(m_transport, &ITransport::statusChanged, this, &TransferManager::onStatusChanged);
    connect(m_transport, &ITransport::incomingRequest, this, &TransferManager::onIncomingRequest);
    connect(m_transport, &ITransport::incomingResolved, this, &TransferManager::onIncomingResolved);
    connect(m_transport, &ITransport::finished, this, &TransferManager::onFinished);
    connect(m_transport, &ITransport::textReceived, this, &TransferManager::textReceived);
    connect(m_transport, &ITransport::fileReceivedAt, this, &TransferManager::onFileReceivedAt);

    m_thread.start();
    // Nota: el servicio de segundo plano NO se arranca aquí (la actividad aún no
    // está en primer plano → Android 12+ lanzaría ForegroundServiceStartNotAllowed).
    // Se arranca desde QML vía startBackgroundReceiver() cuando la app está activa.

    // Habilitar/deshabilitar "Portapapeles" según haya texto (escritorio: en vivo).
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
            this, &TransferManager::clipboardChanged);

    // IP local del equipo, refrescada periódicamente (puede cambiar de red).
    auto refreshIp = [this]() {
        QList<QPair<int, QString>> ranked;
        const QHostAddress routed = routedLocalAddress();
        const auto ifaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &iface : ifaces) {
            if (!(iface.flags() & QNetworkInterface::IsUp) ||
                (iface.flags() & QNetworkInterface::IsLoopBack))
                continue;
            for (const QNetworkAddressEntry &e : iface.addressEntries()) {
                if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                    continue;
                ranked << qMakePair(addrRank(iface, e.ip(), routed), e.ip().toString());
            }
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                             return a.first < b.first;
                         });
        QStringList ordered;
        for (const auto &p : ranked)
            ordered << p.second;
        if (ordered != m_webAddrs) {
            m_webAddrs = ordered;
            if (m_webAddrIndex >= m_webAddrs.size())
                m_webAddrIndex = 0;
            updateWebUrl();
            emit webChanged();
        }
        // La lista visible sigue el mismo orden que el QR, para que no se
        // contradigan (antes iba en orden de enumeración del sistema).
        const QString joined = ordered.join(", ");
        if (joined != m_localIp) {
            m_localIp = joined;
            emit localIpChanged();
        }
    };
    refreshIp();
    m_ipTimer = new QTimer(this);
    m_ipTimer->setInterval(5000);
    connect(m_ipTimer, &QTimer::timeout, this, refreshIp);
    m_ipTimer->start();

    // Seguir el tema del sistema cuando el modo es "automático".
    if (QStyleHints *sh = QGuiApplication::styleHints())
        connect(sh, &QStyleHints::colorSchemeChanged, this,
                [this]() { emit systemDarkChanged(); });
}

TransferManager::~TransferManager()
{
    // Imprescindible: si el miembro m_webThread se destruye con el hilo aún
    // corriendo, Qt aborta con qFatal ("QThread: Destroyed while thread is
    // still running") y en Windows sale "excepción con error inmediato".
    stopWebServer();
    // Cancelar transferencia activa ANTES de intentar terminar el hilo.
    cancel();
    m_thread.quit();
    // En Android, el destructor puede ejecutarse cuando la activity ya está
    // destruida; wait() con timeout evita bloquear el hilo principal.
    m_thread.wait(2000);
}

// Apagado ordenado del servidor web. Lo usan tanto el interruptor de modo web
// como el destructor, para que no haya dos versiones que se desincronicen.
void TransferManager::stopWebServer()
{
    if (!m_web)
        return;
    if (m_webThread.isRunning()) {
        // Parada ENCOLADA, no bloqueante: el hilo la atiende en su bucle de
        // eventos y a continuación quit() lo termina (las dos van a la misma
        // cola, así que se procesan en orden). No hace falta bloquear: el
        // servidor es asíncrono de principio a fin —escribe por trozos con
        // bytesWritten, sin waitForBytesWritten— así que su bucle siempre
        // está libre para atender la parada, incluso sirviendo una descarga.
        QMetaObject::invokeMethod(m_web, "stop", Qt::QueuedConnection);
        m_webThread.quit();
        if (!m_webThread.wait(5000))
            qWarning() << "[Web] el hilo no terminó en 5 s";
    }
    m_web = nullptr;   // lo borra su propio hilo (finished → deleteLater)
}

void TransferManager::setAutoAccept(bool v)
{
    if (m_autoAccept == v)
        return;
    m_autoAccept = v;
    if (m_transport)
        m_transport->setAutoAccept(v);   // atómico: seguro entre hilos
    m_settings.setAutoAccept(v);
    emit autoAcceptChanged();
}

QAbstractItemModel *TransferManager::sendQueue() const
{
    return m_queue;
}

static QString decodePath(const QString &path) {
    if (path.startsWith(QLatin1String("content://")) || path.startsWith(QLatin1String("http://")) || path.startsWith(QLatin1String("https://")))
        return QUrl(path).toLocalFile().isEmpty() ? path : QUrl(path).toLocalFile();
    if (path.contains(QLatin1String("%20")) || path.contains(QLatin1String("%28")) || path.contains(QLatin1String("%29")))
        return QUrl::fromPercentEncoding(path.toUtf8());
    return path;
}

QString TransferManager::computeSummary(const QStringList &paths, const QString &text)
{
    if (paths.isEmpty() && !text.isEmpty())
        return tr("Mensaje de texto");
    if (paths.size() == 1) {
        QFileInfo fi(decodePath(paths.first()));
        if (fi.isDir())
            return tr("Carpeta «%1»").arg(fi.fileName());
        return fi.fileName();
    }
    return tr("%1 elementos").arg(paths.size());
}

QString TransferManager::computeGeneric(const QStringList &paths, const QString &text)
{
    if (paths.isEmpty() && !text.isEmpty())
        return tr("Mensaje de texto");
    if (paths.size() == 1) {
        QFileInfo fi(decodePath(paths.first()));
        return fi.isDir() ? tr("Carpeta") : tr("1 archivo");
    }
    return tr("%1 elementos").arg(paths.size());
}

void TransferManager::enqueueSend(const QString &host, int port, const QString &name,
                                  const QString &platform, const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &u : urls) {
        paths << (u.isLocalFile() ? u.toLocalFile() : decodePath(u.toString(QUrl::FullyEncoded)));
    }
    if (paths.isEmpty())
        return;
    SendQueueModel::Job j;
    j.host = host;
    j.port = static_cast<quint16>(port);
    j.name = name.isEmpty() ? host : name;
    j.platform = platform;
    j.paths = paths;
    j.summary = computeSummary(paths, QString());
    j.genericSummary = computeGeneric(paths, QString());
    j.totalSize = 0;
    for (const QString &p : paths) j.totalSize += QFileInfo(p).size();
    m_queue->addJob(j);
    clearLastResult();
    if (m_state != "sending" && m_state != "receiving") {
        setState("sending");
        m_progress = 0.0;
        m_statusCount.clear();
        m_statusName = tr("En cola…");
        m_statusSize = j.totalSize > 0 ? QString("%1 · %2").arg(j.summary, humanSize(j.totalSize)) : j.summary;
        emit progressChanged();
        setStatusText(tr("Preparando envío: %1").arg(j.summary));
    }
    pumpQueue();
}

void TransferManager::enqueueText(const QString &host, int port, const QString &name,
                                  const QString &platform, const QString &text)
{
    if (text.isEmpty())
        return;
    SendQueueModel::Job j;
    j.host = host;
    j.port = static_cast<quint16>(port);
    j.name = name.isEmpty() ? host : name;
    j.platform = platform;
    j.text = text;
    j.summary = computeSummary(QStringList(), text);
    j.genericSummary = computeGeneric(QStringList(), text);
    m_queue->addJob(j);
    clearLastResult();
    if (m_state != "sending" && m_state != "receiving") {
        setState("sending");
        m_progress = 0.0;
        m_statusCount.clear();
        m_statusName = tr("En cola…");
        m_statusSize = j.summary;
        emit progressChanged();
        setStatusText(tr("Preparando envío: %1").arg(j.summary));
    }
    pumpQueue();
}

QString TransferManager::clipboardText() const
{
#ifdef Q_OS_ANDROID
    const QString t = AndroidStorage::clipboardText();
    if (!t.isEmpty())
        return t;
#endif
    return QGuiApplication::clipboard()->text();
}

void TransferManager::setClipboardText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

// Versión instalada, centralizada en CMake (project(vorlan VERSION …)).
QString TransferManager::appVersion() const
{
#ifdef VORLAN_VERSION
    return QString::fromUtf8(VORLAN_VERSION);
#else
    return QStringLiteral("1.01");
#endif
}

// Comparación numérica por componentes: "1.02" < "1.10".
int TransferManager::compareVersions(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

// Chequeo MANUAL de actualizaciones contra GitHub Releases. Un único GET a
// api.github.com cuando el usuario lo pide; sin red automática ni telemetría.
// Límite anónimo de la API: 60 peticiones/hora por IP (de sobra para uso manual).
void TransferManager::checkForUpdates()
{
    if (m_updateChecking)
        return;   // ya hay una comprobación en curso
    if (!m_updateNet)
        m_updateNet = new QNetworkAccessManager(this);
    m_updateChecking = true;
    emit updateCheckChanged();

    QNetworkRequest req(QUrl(QStringLiteral(
        "https://api.github.com/repos/Jonatan10mil/vorlan/releases/latest")));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("VorLAN/") + appVersion());   // la API exige User-Agent
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(10000);   // 10 s: sin colgar la UI si no hay red
    QNetworkReply *reply = m_updateNet->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_updateChecking = false;
        emit updateCheckChanged();
        if (reply->error() != QNetworkReply::NoError) {
            emit updateCheckFailed();
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = obj.value(QLatin1String("tag_name")).toString().trimmed();
        if (tag.startsWith(QLatin1Char('v')) || tag.startsWith(QLatin1Char('V')))
            tag = tag.mid(1);   // "v1.02" → "1.02"
        const QString url = obj.value(QLatin1String("html_url")).toString();
        // Sin tag o sin URL (p.ej. aún no hay Releases publicados) no se puede
        // afirmar nada: se informa como fallo, no como "al día".
        if (tag.isEmpty() || url.isEmpty()) {
            emit updateCheckFailed();
            return;
        }
        if (compareVersions(appVersion(), tag) < 0)
            emit updateAvailable(tag, url);
        else
            emit updateUpToDate();
    });
}

void TransferManager::enqueueClipboard(const QString &host, int port, const QString &name,
                                       const QString &platform)
{
#ifdef Q_OS_ANDROID
    // En Android el portapapeles se usa solo para texto (una captura NO va al
    // portapapeles, va a la galería → se comparte con el botón "Compartir").
    QString t = AndroidStorage::clipboardText();
    if (t.isEmpty())
        t = QGuiApplication::clipboard()->text();
    enqueueText(host, port, name, platform, t);
#else
    // En escritorio sí puede haber una imagen en el portapapeles (Ctrl+C / captura) → enviarla.
    const QClipboard *cb = QGuiApplication::clipboard();
    const QImage img = cb->image();
    if (!img.isNull()) {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (dir.isEmpty())
            dir = QDir::tempPath();
        QDir().mkpath(dir);
        const QString path = dir + "/captura-"
            + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png";
        if (img.save(path, "PNG")) {
            enqueueSend(host, port, name, platform, QList<QUrl>{ QUrl::fromLocalFile(path) });
            return;
        }
    }
    enqueueText(host, port, name, platform, cb->text());
#endif
}

void TransferManager::cancelJob(int id)
{
    const SendQueueModel::Job *j = m_queue->jobById(id);
    if (!j)
        return;
    if (id == m_currentJobId) {
        cancel();   // aborta el envío en curso (el worker emitirá finished)
    } else if (j->status == SendQueueModel::Queued) {
        m_queue->setStatus(id, SendQueueModel::Canceled);
        scheduleRemoval(id);
    }
}

void TransferManager::scheduleRemoval(int id)
{
    // El envío terminado (enviado/error/cancelado) desaparece solo tras unos segundos.
    QTimer::singleShot(10000, this, [this, id]() { m_queue->removeById(id); });
}

void TransferManager::clearFinishedSends()
{
    m_queue->clearFinished();
}

void TransferManager::pumpQueue()
{
    if (m_currentJobId != -1)          // ya hay un envío en curso
        return;
    if (m_state == "receiving" || m_incomingActive)   // esperar a terminar de recibir
        return;
    const int id = m_queue->firstQueuedId();
    if (id == -1)
        return;
    const SendQueueModel::Job *j = m_queue->jobById(id);
    if (!j)
        return;
    m_currentJobId = id;
    m_queue->setStatus(id, SendQueueModel::Sending);
    doSend(j->host, j->port, j->paths, j->text);
}

void TransferManager::doSend(const QString &host, int port, const QStringList &paths, const QString &text)
{
    setState("sending");
    m_progress = 0.0;
    m_statusCount.clear();
    m_statusName = tr("Preparando…");
    m_statusSize = tr("Cargando archivos…");
    emit progressChanged();
    setStatusText(tr("Preparando…"));
    m_lastNotifPercent = -1;
    AndroidNotify::start(true, tr("Preparando envío…"));
    emit requestSendItems(host, static_cast<quint16>(port), paths, text);
}

void TransferManager::sendPaths(const QString &host, int port, const QList<QUrl> &urls)
{
    enqueueSend(host, port, QString(), QString(), urls);
}

void TransferManager::sendText(const QString &host, int port, const QString &text)
{
    enqueueText(host, port, QString(), QString(), text);
}

void TransferManager::sendFile(const QString &host, int port, const QUrl &fileUrl)
{
    enqueueSend(host, port, QString(), QString(), QList<QUrl>{fileUrl});
}

void TransferManager::respond(bool accept)
{
    if (m_pendingWebConn) {
        if (m_web) {
            QMetaObject::invokeMethod(m_web, "respondToWebRequest",
                Q_ARG(quintptr, m_pendingWebConn),
                Q_ARG(bool, accept));
        }
        m_pendingWebConn = 0;
        onIncomingResolved();   // ocultar el diálogo en la UI
    } else if (m_transport) {
        m_transport->respondDecision(accept);
    }
}

void TransferManager::cancel()
{
    if (m_transport)
        m_transport->requestCancel();
    if (m_web)
        QMetaObject::invokeMethod(m_web, "cancelUploads");
}

void TransferManager::sendClipboard(const QString &host, int port)
{
    enqueueClipboard(host, port, QString(), QString());
}

QString TransferManager::downloadsPath() const
{
    return FileActions::downloadsPath(m_downloadDir);
}

void TransferManager::setThemeMode(const QString &m)
{
    if (m == m_themeMode)
        return;
    m_themeMode = m;
    m_settings.setThemeMode(m);
    emit themeChanged();
}

void TransferManager::setLanguage(const QString &code)
{
    if (code == m_language)
        return;
    m_language = code;
    m_settings.setLanguage(code);
    emit languageChanged();   // main.cpp instala el QTranslator y llama a engine.retranslate()
}

bool TransferManager::systemDark() const
{
    if (QStyleHints *sh = QGuiApplication::styleHints())
        return sh->colorScheme() == Qt::ColorScheme::Dark;
    return true;
}

void TransferManager::setShowFileNames(bool v)
{
    if (m_showFileNames == v)
        return;
    m_showFileNames = v;
    if (m_transport)
        m_transport->setShowNames(v);
    m_settings.setShowFileNames(v);
    emit showFileNamesChanged();
}

void TransferManager::setCloseToTray(bool v)
{
    if (m_closeToTray == v)
        return;
    m_closeToTray = v;
    m_settings.setCloseToTray(v);
    emit closeToTrayChanged();
}

void TransferManager::setNotificationsEnabled(bool v)
{
    if (m_notificationsEnabled == v)
        return;
    m_notificationsEnabled = v;
    m_settings.setNotificationsEnabled(v);
    emit notificationsEnabledChanged();
}

bool TransferManager::batteryExempt() const
{
    return AndroidStorage::isBatteryExempt();
}

void TransferManager::setDiscoverable(bool v)
{
    if (m_discoverable == v)
        return;
    m_discoverable = v;
    m_settings.setDiscoverable(v);
    emit discoverableChanged();   // main.cpp lo conecta a Discovery::setVisible
}

bool TransferManager::sslAvailable() const
{
    return QSslSocket::supportsSsl();
}

void TransferManager::setEncrypt(bool v)
{
    if (m_encrypt == v)
        return;
    m_encrypt = v;
    m_settings.setEncrypt(v);
    if (m_transport)
        m_transport->setEncrypt(v);
    emit encryptChanged();
}

void TransferManager::requestBatteryExemption()
{
    AndroidStorage::requestBatteryExemption();
    // El resultado se sabrá al volver a primer plano; startBackgroundReceiver()
    // (ApplicationActive) reemite batteryExemptChanged para refrescar la UI.
}

void TransferManager::startBackgroundReceiver()
{
    // Llamado desde QML cuando la app vuelve a primer plano.
    // NO se arranca ningún servicio en primer plano "en reposo": el
    // FGS solo existe DURANTE una transferencia. Un FGS en reposo mantenía vivo el
    // proceso al deslizar la app de Recientes y, al reabrir, Qt reutilizaba una
    // superficie destruida → cuelgue. Mientras se minimiza (sin cerrar) el bucle de
    // Qt sigue vivo (background_running=true), así que la escucha no se pierde.
    // La exención de batería pudo cambiar en Ajustes del sistema → refrescar el botón.
    emit batteryExemptChanged();
}

void TransferManager::setAccentColor(const QString &c)
{
    if (c == m_accentColor)
        return;
    m_accentColor = c;
    m_settings.setAccentColor(c);
    emit accentChanged();
}

// Miniatura del avatar (cuadrada 40x40 JPEG) en base64, para difundirla a los demás
// dispositivos por UDP. Caber dentro de un datagrama UDP (~1400 bytes útil).
QString TransferManager::avatarThumb() const
{
    const QString u = avatarImage();
    if (u.isEmpty())
        return QString();
    const QUrl url(u);
    const QString path = url.isLocalFile() ? url.toLocalFile() : u;
    QImage img(path);
    if (img.isNull())
        return QString();
    const int s = qMin(img.width(), img.height());
    if (s <= 0)
        return QString();
    img = img.copy((img.width() - s) / 2, (img.height() - s) / 2, s, s)
             .scaled(40, 40, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPEG", 50);
    buf.close();
    if (bytes.isEmpty() || bytes.size() > 1024)   // caber en un solo datagrama UDP
        return QString();
    return QString::fromLatin1(bytes.toBase64());
}

void TransferManager::setAvatarImage(const QUrl &url)
{
    const QString u = url.isEmpty() ? QString() : url.toString();
    if (u == m_avatarImage)
        return;
    m_avatarImage = u;
    m_settings.setAvatarImage(u);
    emit avatarImageChanged();
}

void TransferManager::resetProfile()
{
    // Avatar: quitar la imagen elegida → vuelve a la foto del SO.
    if (!m_avatarImage.isEmpty()) {
        m_avatarImage.clear();
        m_settings.removeAvatarImage();
        emit avatarImageChanged();
    }
    // Nombre: volver al nombre por defecto.
    if (!m_defaultName.isEmpty())
        setDeviceName(m_defaultName);
}

QString TransferManager::hostName() const
{
#ifdef Q_OS_ANDROID
    const QString m = AndroidDevice::deviceModel();
    if (!m.isEmpty())
        return m;
#endif
    // Nombre del equipo = salida del comando `hostname` (gethostname()).
    QString h = QSysInfo::machineHostName();
    if (h.isEmpty())
        h = QHostInfo::localHostName();
    if (h.isEmpty())
        h = QStringLiteral("PC");
    return h;
}

QString TransferManager::deviceType() const
{
    return detectDeviceType();
}

bool TransferManager::isTv() const
{
    return AndroidDevice::isTelevision();
}

void TransferManager::setDownloadDir(const QUrl &url)
{
    QString path;
    if (url.isLocalFile())
        path = url.toLocalFile();
    else {
        path = url.toString();
        // Normaliza valores tipo "file:///..." que isLocalFile no detecta como tal
        // (p.ej. cuando QUrl se construye desde string ya con esquema).
        if (path.startsWith(QLatin1String("file://"))) {
            const QString local = QUrl(path).toLocalFile();
            if (!local.isEmpty())
                path = local;
        }
    }
    // Android usa content:// — no normalizar, se debe preservar tal cual.
    if (!path.startsWith(QLatin1String("content://")))
        path = QDir::cleanPath(path);
    if (path == m_downloadDir)
        return;
    m_downloadDir = path;
    if (m_transport)
        m_transport->setDownloadDir(path);
    if (m_web)
        QMetaObject::invokeMethod(m_web, "setDownloadDir", Q_ARG(QString, path));
    m_settings.setDownloadDir(path);
    emit downloadDirChanged();
}

void TransferManager::enqueueSendPaths(const QString &host, int port, const QString &name,
                                      const QString &platform, const QStringList &paths)
{
    // Rutas locales o content:// → QUrl bien formada (fromLocalFile codifica espacios,
    // paréntesis, etc.). Robusto para archivos Y carpetas compartidos.
    QList<QUrl> urls;
    for (const QString &p : paths) {
        if (p.isEmpty())
            continue;
        urls << (p.startsWith(QLatin1String("content://")) ? QUrl(p) : QUrl::fromLocalFile(p));
    }
    if (!urls.isEmpty())
        enqueueSend(host, port, name, platform, urls);
}

void TransferManager::pickAndroidFiles(const QString &host, int port, const QString &name,
                                       const QString &platform)
{
    AndroidStorage::pickFiles([this, host, port, name, platform](const QStringList &uris) {
        if (uris.isEmpty())                // cancelado → no hacer nada
            return;
        enqueueSendPaths(host, port, name, platform, uris);
        emit androidFilesPicked();         // ahora sí: cerrar la página de envío
    });
}

bool TransferManager::shouldPromptFolder() const
{
#ifdef Q_OS_ANDROID
    // En Android TV NO se pide: no hay pantalla táctil y muchas TV ni siquiera
    // traen el selector de carpetas del sistema, así que el diálogo dejaba la app
    // bloqueada. Se usa la carpeta pública Descargas/Vorlan (vía MediaStore).
    if (isTv())
        return false;
    // Obligatorio: pedir carpeta mientras no haya una carpeta SAF elegida.
    return !m_downloadDir.startsWith(QLatin1String("content://"));
#elif defined(Q_OS_IOS)
    // iOS no tiene selector de carpetas: se recibe en el sandbox de la app.
    return false;
#else
    // Escritorio: obligatorio elegir carpeta la primera vez. En cuanto hay una
    // guardada en los ajustes no se vuelve a preguntar.
    return m_downloadDir.isEmpty();
#endif
}

void TransferManager::markFolderPrompted()
{
    // No-op: shouldPromptFolder ya no usa folderPrompted, solo m_downloadDir.
    // Se mantiene el método para QML legacy; no persiste nada.
}

void TransferManager::pickAndroidFolder()
{
    AndroidStorage::pickFolder([this](const QString &treeUri) {
        if (treeUri.isEmpty())
            return;   // cancelado
        // El callback llega en el hilo GUI; actualizar la carpeta elegida.
        m_downloadDir = treeUri;
        if (m_transport)
            m_transport->setDownloadDir(treeUri);
        m_settings.setDownloadDir(treeUri);
        emit downloadDirChanged();
    });
}

void TransferManager::applyStatusBar(bool appDark)
{
#ifdef Q_OS_ANDROID
    AndroidStorage::applySystemBars(!appDark);   // app clara → fondo claro → iconos oscuros
#else
    Q_UNUSED(appDark)
#endif
}

bool TransferManager::takeOpenReceived()
{
    return AndroidStorage::takeOpenReceived();
}

QString TransferManager::takeSharedText()
{
    return AndroidStorage::takeSharedText();
}

QString TransferManager::takeSharedFiles()
{
#ifdef Q_OS_ANDROID
    return AndroidStorage::takeSharedFiles();
#else
    return QString();
#endif
}

QString TransferManager::installedApps()
{
#ifdef Q_OS_ANDROID
    return AndroidStorage::installedApps();
#else
    return QStringLiteral("[]");
#endif
}

void TransferManager::sendApp(const QString &host, int port, const QString &name,
                              const QString &platform, const QString &apkPath, const QString &label)
{
#ifdef Q_OS_ANDROID
    // Copiar el APK a la caché con un nombre legible y enviarlo.
    const QString staged = AndroidStorage::stageApk(apkPath, label);
    const QString path = staged.isEmpty() ? apkPath : staged;
    enqueueSend(host, port, name, platform, QList<QUrl>{ QUrl::fromLocalFile(path) });
#else
    Q_UNUSED(host) Q_UNUSED(port) Q_UNUSED(name)
    Q_UNUSED(platform) Q_UNUSED(apkPath) Q_UNUSED(label)
#endif
}

void TransferManager::sendApps(const QString &host, int port, const QString &name,
                               const QString &platform, const QStringList &apkPaths,
                               const QStringList &labels)
{
#ifdef Q_OS_ANDROID
    QList<QUrl> urls;
    for (int i = 0; i < apkPaths.size(); ++i) {
        const QString label = (i < labels.size()) ? labels.at(i) : QString();
        const QString staged = AndroidStorage::stageApk(apkPaths.at(i), label);
        urls << QUrl::fromLocalFile(staged.isEmpty() ? apkPaths.at(i) : staged);
    }
    if (!urls.isEmpty())
        enqueueSend(host, port, name, platform, urls);
#else
    Q_UNUSED(host) Q_UNUSED(port) Q_UNUSED(name)
    Q_UNUSED(platform) Q_UNUSED(apkPaths) Q_UNUSED(labels)
#endif
}

// "Hay algo que enviar en el portapapeles": texto en cualquier plataforma, o imagen en escritorio.
bool TransferManager::clipboardHasText() const
{
#ifdef Q_OS_ANDROID
    if (!AndroidStorage::clipboardText().isEmpty())
        return true;
    return !QGuiApplication::clipboard()->text().isEmpty();
#else
    const QClipboard *cb = QGuiApplication::clipboard();
    return !cb->text().isEmpty() || !cb->image().isNull();
#endif
}

void TransferManager::openDownloadsFolder()
{
    FileActions::openDownloads(m_downloadDir);
}

void TransferManager::openPath(const QString &path)
{
    // Archivo → app predeterminada del sistema; carpeta → explorador de archivos.
    FileActions::openPath(path, m_downloadDir);
}

void TransferManager::openContainingFolder(const QString &path)
{
    FileActions::openContaining(path, m_downloadDir);
}

// Carpeta REAL donde quedó guardado un recibido (para mostrarla en la lista),
// derivada de la ruta real del archivo — no de un texto fijo.
QString TransferManager::savedFolderLabel(const QString &path) const
{
    // Android SAF (content://): mostrar el nombre de la carpeta elegida.
    if (path.isEmpty() || path.startsWith(QLatin1String("content://")))
        return FileActions::downloadsPath(m_downloadDir);
    QString p = path;
    if (p.startsWith(QLatin1String("file://")))
        p = QUrl(p).toLocalFile();
    QFileInfo fi(p);
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    return dir.isEmpty() ? FileActions::downloadsPath(m_downloadDir) : dir;
}

void TransferManager::onFileReceivedAt(const QString &path)
{
    if (!path.isEmpty())
        m_receivedPaths << path;
}

void TransferManager::shareFile(const QString &path)
{
    FileActions::share(path);
}

bool TransferManager::isDir(const QString &path) const
{
    return FileActions::isDir(path);
}

void TransferManager::setDeviceName(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || n == m_deviceName)
        return;
    m_deviceName = n;
    if (m_transport)
        m_transport->setSelfName(n);
    m_settings.setDeviceName(n);
    emit deviceNameChanged();
}

void TransferManager::onProgress(qint64 done, qint64 total, const QString &name,
                                 int curFile, int totFiles)
{
    m_progress = total > 0 ? double(done) / double(total) : 0.0;
    // Dos columnas: nombre (col1) y tamaño (col2) — tamaño siempre visible.
    m_statusCount = totFiles > 1 ? QStringLiteral("(%1/%2)").arg(qMin(curFile, totFiles)).arg(totFiles) : QString();
    m_statusName = m_showFileNames ? name : QString();
    m_statusSize = QString("%1 / %2").arg(humanSize(done), humanSize(total));
    emit progressChanged();
    // Compat: statusText legado para notificaciones/otros
    const QString count = m_statusCount.isEmpty() ? QString() : m_statusCount + "  ";
    const QString status = m_showFileNames
        ? QString("%1%2  ·  %3 / %4").arg(count, name, humanSize(done), humanSize(total))
        : QString("%1%2 / %3").arg(count, humanSize(done), humanSize(total));
    setStatusText(status);

    // Progreso de la fila de la cola (envío en curso).
    if (m_currentJobId != -1)
        m_queue->setProgress(m_currentJobId, m_progress);

    // OJO: la NOTIFICACIÓN la actualiza el propio Worker desde su hilo. Si se
    // hiciera aquí (hilo de UI), al bloquear la pantalla Qt lo suspende y la barra
    // se quedaba congelada aunque la transferencia siguiera.
}

// Guardando el archivo en la carpeta final (Android copia el archivo completo):
// puede tardar, así que se avisa con barra indeterminada en vez de dejarla al 100%.
void TransferManager::onSavingFile(const QString &name, int curFile, int totFiles)
{
    // Mantener el contador "(3/14)" para que no desaparezca al guardar.
    m_statusCount = totFiles > 1 ? QStringLiteral("(%1/%2)").arg(qMin(curFile, totFiles)).arg(totFiles) : QString();
    m_statusName = m_showFileNames ? name : QString();
    m_statusSize = tr("Guardando…");
    emit progressChanged();
    const QString count = m_statusCount.isEmpty() ? QString() : m_statusCount + "  ";
    const QString status = count + (m_showFileNames ? tr("Guardando %1…").arg(name) : tr("Guardando…"));
    setStatusText(status);   // la notificación la actualiza el Worker (su hilo)
}

void TransferManager::onStatusChanged(const QString &state)
{
    // La notificación de envío la arranca doSend(); aquí solo la de recepción.
    if (state == "receiving" && m_state != "receiving") {
        m_lastNotifPercent = -1;
        m_receivedPaths.clear();
        m_incomingIsFolder = false;
        // Limpiar datos de la transferencia anterior para que la UI no muestre
        // un flash con el nombre/progreso del archivo viejo.
        m_statusName.clear();
        m_statusSize.clear();
        m_statusCount.clear();
        m_progress = 0.0;
        AndroidNotify::start(false, tr("Recibiendo archivos…"));
    }
    setState(state);
}

void TransferManager::onIncomingRequest(const QString &senderName, const QString &summary, qint64 size, int items)
{
    clearLastResult();
    m_incomingActive = true;
    m_incomingName = senderName.isEmpty() ? QStringLiteral("Dispositivo") : senderName;
    m_incomingSummary = summary;
    m_incomingSizeText = humanSize(size);
    m_incomingItems = items;
    emit incomingChanged();
    emit incomingRequested(m_incomingName, summary);
}

void TransferManager::onIncomingResolved()
{
    if (!m_incomingActive)
        return;
    m_incomingActive = false;
    emit incomingChanged();
}

void TransferManager::onFinished(bool ok, const QString &direction, const QString &summary)
{
    // ¿Terminó el envío en curso de la cola?
    if (m_currentJobId != -1) {
        const int jid = m_currentJobId;
        const SendQueueModel::Status st =
            ok ? SendQueueModel::Done
               : (summary.startsWith("Cancelado") ? SendQueueModel::Canceled
                                                   : SendQueueModel::Error);
        if (ok)
            m_queue->setProgress(jid, 1.0);
        m_queue->setStatus(jid, st);
        m_currentJobId = -1;
        scheduleRemoval(jid);   // se descarta solo tras unos segundos
    }

    setState(ok ? "done" : "error");
    setStatusText(summary);
    emit transferDone(ok, direction, summary);

    // Que la recepción se quede mostrándose un momento en el banner.
    // (Los envíos ya se muestran en la lista de "Envíos", no necesitan banner.)
    if (direction.startsWith(QLatin1String("received")))
        setLastResult(ok ? QStringLiteral("received") : QStringLiteral("error"), summary);

    // Archivo/carpeta recibido: pasar a la UI la ruta a abrir.
    //  - 1 archivo suelto → esa ruta (se abre con la app predeterminada).
    //  - varios / carpeta → la carpeta de descargas (se abre el explorador).
    //  (En Android la copia interna se borra tras exportar → abrir la carpeta.)
    if (ok && direction == QLatin1String("received")) {
        // ¿Carpeta o varios archivos? Entonces NO hay "un archivo" que abrir: la UI
        // debe abrir la carpeta de recibidos (y no ofrecer "Compartir").
        const bool isFolder = m_incomingIsFolder || m_receivedPaths.size() > 1;
        QString openTarget;
        if (!isFolder && !m_receivedPaths.isEmpty())
            openTarget = m_receivedPaths.first();   // un único archivo suelto
#ifndef Q_OS_ANDROID
        if (isFolder || openTarget.isEmpty())
            openTarget = downloadsPath();           // escritorio: abrir la carpeta
#endif
        emit receivedFile(summary, openTarget, isFolder || m_receivedPaths.isEmpty(),
                          m_incomingName,
                          isFolder ? 0 : QFileInfo(openTarget).size(),
                          QDateTime::currentDateTime());
    }

    // Aviso de resultado (Android: notificación breve; no-op en escritorio,
    // donde de esto se encarga la bandeja del sistema).
    QString notifTitle;
    if (direction == QLatin1String("received_text"))
        notifTitle = tr("Mensaje recibido");
    else if (direction.startsWith(QLatin1String("received")))
        notifTitle = ok ? tr("Archivo recibido")
                        : tr("Recepción fallida");
    else
        notifTitle = ok ? tr("Envío completado")
                        : (summary.startsWith(QLatin1String("Cancelado"))
                               ? tr("Envío cancelado")
                               : tr("Envío fallido"));
    if (m_notificationsEnabled)
        AndroidNotify::result(notifTitle, summary);

    // Limpieza cache Android: no deja rastro tras enviar/cancelar.
    // Antes quedaba cache/shared+outgoing hasta el siguiente envío (876 MB).
    if (direction == QLatin1String("sent")) {
        AndroidStorage::clearOutgoingCache();
        AndroidStorage::clearSharedCache();
    }
    // Al terminar (nada más en cola): parar el servicio en primer plano.
    // El FGS solo vive durante la transferencia; en reposo no hay servicio,
    // así que deslizar de Recientes mata el proceso y la próxima apertura es limpia.
    if (m_queue->firstQueuedId() == -1)
        AndroidNotify::stop();
    pumpQueue();
}

void TransferManager::setState(const QString &s)
{
    if (m_state == s)
        return;
    m_state = s;
    // Al empezar una transferencia nueva, quitar el resultado persistente anterior.
    if ((s == QLatin1String("sending") || s == QLatin1String("receiving"))
        && !m_lastResult.isEmpty()) {
        m_lastResult.clear();
        m_lastResultText.clear();
        if (m_resultTimer)
            m_resultTimer->stop();
        emit lastResultChanged();
    }
    emit stateChanged();
}

void TransferManager::clearLastResult()
{
    if (m_lastResult.isEmpty() && m_lastResultText.isEmpty() && m_statusName.isEmpty() && m_statusSize.isEmpty() && m_statusCount.isEmpty() && m_progress == 0.0)
        return;
    m_lastResult.clear();
    m_lastResultText.clear();
    m_statusName.clear();
    m_statusSize.clear();
    m_statusCount.clear();
    m_progress = 0.0;
    if (m_resultTimer) m_resultTimer->stop();
    emit lastResultChanged();
    emit progressChanged();
    emit statusTextChanged();
}

// Mantiene un resultado ("received"/"sent"/"error") visible unos segundos tras
// terminar, para que el receptor vea el fin igual que el emisor.
void TransferManager::setLastResult(const QString &kind, const QString &text)
{
    m_lastResult = kind;
    m_lastResultText = text;
    emit lastResultChanged();
    if (!m_resultTimer) {
        m_resultTimer = new QTimer(this);
        m_resultTimer->setSingleShot(true);
        connect(m_resultTimer, &QTimer::timeout, this, [this]() {
            m_lastResult.clear();
            m_lastResultText.clear();
            emit lastResultChanged();
        });
    }
    m_resultTimer->start(4500);
}

void TransferManager::setStatusText(const QString &s)
{
    if (m_statusText == s)
        return;
    m_statusText = s;
    emit statusTextChanged();
}

QString TransferManager::humanSize(qint64 bytes)
{
    return QLocale(QLocale::English).formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// ============================================================================
//  Modo web: un servidor HTTP propio permite que cualquier dispositivo con
//  navegador (sin instalar VorLAN) suba archivos aquí y descargue los que se
//  compartan. Corre en su propio hilo para no bloquear la interfaz.
// ============================================================================

QString TransferManager::qrSvgUri(const QString &text) const
{
    const std::string svg = qr::toSvg(text.toStdString());
    if (svg.empty())
        return QString();
    // Image { source: "data:image/svg+xml;utf8,<svg…>" }
    return QStringLiteral("data:image/svg+xml;utf8,") +
           QString::fromStdString(svg);
}

void TransferManager::setWebEnabled(bool v)
{
    if (m_webEnabled == v)
        return;
    m_webEnabled = v;

    m_webError.clear();
    if (!v) {
        stopWebServer();
        m_webPort = 0;
        m_webUrl.clear();
        emit webChanged();
        return;
    }

    if (!m_web) {
        m_web = new WebServer;
        // Fijar saveDir antes de mover al hilo para evitar carrera (subida inmediata antes de que el queued setSaveDir llegue)
        m_web->setSaveDir(webSaveDir());
#ifdef Q_OS_ANDROID
        QString pub;
        QString p = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (!p.isEmpty()) { pub = p + "/Vorlan"; QDir().mkpath(pub); }
        m_web->setPublicDir(pub);
        m_web->setDownloadDir(m_downloadDir);
#endif
        m_web->moveToThread(&m_webThread);
        // El servidor se destruye DENTRO de su propio hilo, que es donde viven
        // su QTcpServer y sus QTcpSocket (un QObject de socket no debe morir en
        // otro hilo). Es el idiom oficial de QThread para objetos trabajadores.
        connect(&m_webThread, &QThread::finished, m_web, &QObject::deleteLater);
        connect(m_web, &WebServer::uploadFinished, this, &TransferManager::onWebUploadFinished);
        
        connect(m_web, &WebServer::incomingWebRequest, this, [this](quintptr connId, const QString &senderName, qint64 size, int items) {
            if (m_autoAccept) {
                QMetaObject::invokeMethod(m_web, "respondToWebRequest",
                    Q_ARG(quintptr, connId), Q_ARG(bool, true));
            } else {
                m_pendingWebConn = connId;
                const QString summary = items > 1 ? tr("%1 archivos").arg(items)
                                      : items == 1 ? tr("1 archivo") : tr("Transferencia");
                onIncomingRequest(senderName, summary, size, items);
            }
        });

        connect(m_web, &WebServer::uploadProgress, this, [this](qint64 done, qint64 total, const QString &name, int curFile, int totFiles) {
            m_statusCount = totFiles > 1 ? QStringLiteral("(%1/%2)").arg(qMin(curFile, totFiles)).arg(totFiles) : QString();
            m_statusName = m_showFileNames ? name : QString();
            m_statusSize = QString("%1 / %2").arg(humanSize(qMax(done, qint64(0))), humanSize(qMax(total, qint64(0))));
            if (total > 0) {
                m_progress = double(done) / double(total);
            } else {
                m_progress = 0.0;
            }

            if (m_state != "receiving") {
                // Nuevo upload: aunque el anterior terminara (m_webUploadDone=true),
                // tratamos cualquier señal en estado != receiving como comienzo de uno nuevo.
                // El flag solo se usaría para proteger señales tardías tras cancelación,
                // pero en la práctica uploadFinished siempre llega después del último progress.
                m_webUploadDone = false;
                m_webLastNotifPct = -1;
                onStatusChanged("receiving");
            }
            
            emit progressChanged();
            emit statusTextChanged();
            const QString count = m_statusCount.isEmpty() ? QString() : m_statusCount + "  ";
            const QString humanDone = humanSize(qMax(done, qint64(0)));
            const QString humanTotal = humanSize(qMax(total, qint64(0)));
            const QString status = m_showFileNames
                ? QString("%1%2  ·  %3 / %4").arg(count, name, humanDone, humanTotal)
                : QString("%1%2 / %3").arg(count, humanDone, humanTotal);
            setStatusText(status);

            if (total > 0) {
                const int pct = static_cast<int>(done * 100 / total);
                if (m_webLastNotifPct < 0) {
                    m_webLastNotifPct = 0;
                    AndroidNotify::start(false, name.isEmpty() ? tr("Recibiendo desde web…") : name);
                }
                if (pct != m_webLastNotifPct) {
                    m_webLastNotifPct = pct;
                    AndroidNotify::update(false, name, pct);
                }
            }
        });
        connect(m_web, &WebServer::failed, this, [this](const QString &e) {
            qWarning() << "[Web] no se pudo iniciar:" << e;
            m_webEnabled = false;
            m_webUrl.clear();
            // Explicar el motivo en pantalla (antes se apagaba en silencio).
            m_webError = tr("No se pudo iniciar: %1").arg(e);
            emit webChanged();
        });
        connect(m_web, &WebServer::started, this, [this](quint16 port) {
            m_webPort = port;
            updateWebUrl();
            emit webChanged();
        });
        m_webThread.start();
    }
    // Configurar y arrancar en el hilo del servidor.
    const QString saveDir = webSaveDir();
    QMetaObject::invokeMethod(m_web, "setSaveDir", Q_ARG(QString, saveDir));
#ifdef Q_OS_ANDROID
    // En Android, la carpeta pública (Downloads/Vorlan) persiste aunque la
    // caché se limpie. WebServer la necesita para deduplicar carpetas al subir.
    QString pubDir;
    QString pub = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!pub.isEmpty()) { pubDir = pub + "/Vorlan"; QDir().mkpath(pubDir); }
    QMetaObject::invokeMethod(m_web, "setPublicDir", Q_ARG(QString, pubDir));
#endif
    QMetaObject::invokeMethod(m_web, "setDeviceName", Q_ARG(QString, m_deviceName));
    QMetaObject::invokeMethod(m_web, "setAccent", Q_ARG(QString, m_accentColor));
    QMetaObject::invokeMethod(m_web, "setPin", Q_ARG(QString, m_webPinEnabled ? m_webPin : QString()));
    QMetaObject::invokeMethod(m_web, "setUseTls", Q_ARG(bool, m_webTls));
    QMetaObject::invokeMethod(m_web, "setSharedFiles", Q_ARG(QStringList, m_webShared));
    QMetaObject::invokeMethod(m_web, "setSharedText", Q_ARG(QString, m_webText));
    QMetaObject::invokeMethod(m_web, "start", Q_ARG(quint16, quint16(Proto::kWebPort)));
    emit webChanged();
}

void TransferManager::setWebShared(const QList<QUrl> &urls)
{
    m_webShared.clear();
    for (const QUrl &u : urls) {
#ifdef Q_OS_ANDROID
        if (!u.isLocalFile() && u.scheme() == QLatin1String("content")) {
            const QString resolved = AndroidStorage::contentToCache(u.toString());
            if (!resolved.isEmpty()) { m_webShared << resolved; continue; }
            continue;   // content:// no resuelto → omitir (no se puede servir)
        }
#endif
        m_webShared << (u.isLocalFile() ? u.toLocalFile() : u.toString(QUrl::FullyEncoded));
    }
    if (m_web)
        QMetaObject::invokeMethod(m_web, "setSharedFiles", Q_ARG(QStringList, m_webShared));
    emit webChanged();
}

void TransferManager::addWebShared(const QList<QUrl> &urls)
{
    for (const QUrl &u : urls) {
#ifdef Q_OS_ANDROID
        if (!u.isLocalFile() && u.scheme() == QLatin1String("content")) {
            const QString resolved = AndroidStorage::contentToCache(u.toString());
            if (!resolved.isEmpty() && !m_webShared.contains(resolved)) {
                m_webShared << resolved;
            }
            continue;   // content:// siempre se resuelve aquí (o se descarta)
        }
#endif
        const QString p = u.isLocalFile() ? u.toLocalFile() : u.toString(QUrl::FullyEncoded);
        if (!p.isEmpty() && !m_webShared.contains(p))
            m_webShared << p;
    }
    if (m_web)
        QMetaObject::invokeMethod(m_web, "setSharedFiles", Q_ARG(QStringList, m_webShared));
    emit webChanged();
}

void TransferManager::addWebSharedPaths(const QStringList &paths)
{
    for (const QString &p : paths) {
        if (p.isEmpty()) continue;
#ifdef Q_OS_ANDROID
        if (p.startsWith(QLatin1String("content://"))) {
            const QString resolved = AndroidStorage::contentToCache(p);
            if (!resolved.isEmpty() && !m_webShared.contains(resolved)) {
                m_webShared << resolved;
                continue;
            }
        }
#endif
        if (!m_webShared.contains(p))
            m_webShared << p;
    }
    if (m_web)
        QMetaObject::invokeMethod(m_web, "setSharedFiles", Q_ARG(QStringList, m_webShared));
    emit webChanged();
}

void TransferManager::addWebSharedApps(const QStringList &apkPaths, const QStringList &labels)
{
#ifdef Q_OS_ANDROID
    // La ruta real de una app instalada es ".../base.apk"; se copia a la caché
    // con el nombre de la app para que se descargue como "WhatsApp.apk".
    QStringList staged;
    for (int i = 0; i < apkPaths.size(); ++i) {
        const QString label = labels.value(i);
        const QString path = AndroidStorage::stageApk(apkPaths.at(i), label);
        staged << (path.isEmpty() ? apkPaths.at(i) : path);
    }
    addWebSharedPaths(staged);
#else
    Q_UNUSED(labels)
    addWebSharedPaths(apkPaths);
#endif
}

void TransferManager::setWebPin(const QString &pin)
{
    const QString p = pin.trimmed();
    bool enabled = !p.isEmpty();
    if (p == m_webPin && enabled == m_webPinEnabled)
        return;
    m_webPin = p;
    m_webPinEnabled = enabled;
    m_settings.setWebPin(p);
    m_settings.setWebPinEnabled(enabled);
    if (m_web)
        QMetaObject::invokeMethod(m_web, "setPin", Q_ARG(QString,
            m_webPinEnabled ? m_webPin : QString()));
    emit webChanged();
}

bool TransferManager::setWebPinEnabled(bool enabled)
{
    if (enabled) {
        if (m_webPin.isEmpty()) return false;   // sin PIN guardado → hay que ponerlo antes
        if (m_webPinEnabled) return true;
        m_webPinEnabled = true;
        m_settings.setWebPinEnabled(true);
        if (m_web)
            QMetaObject::invokeMethod(m_web, "setPin", Q_ARG(QString, m_webPin));
        emit webChanged();
        return true;
    } else {
        if (!m_webPinEnabled) return true;
        m_webPinEnabled = false;
        m_settings.setWebPinEnabled(false);
        if (m_web)
            QMetaObject::invokeMethod(m_web, "setPin", Q_ARG(QString, QString()));
        emit webChanged();
        return true;
    }
}

void TransferManager::setWebText(const QString &text)
{
    m_webText = text;
    if (m_web)
        QMetaObject::invokeMethod(m_web, "setSharedText", Q_ARG(QString, m_webText));
    emit webChanged();
}

void TransferManager::shareClipboardWeb()
{
#ifndef Q_OS_ANDROID
    // En escritorio puede haber una imagen en el portapapeles (Ctrl+C / captura).
    const QClipboard *cb = QGuiApplication::clipboard();
    const QImage img = cb->image();
    if (!img.isNull()) {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (dir.isEmpty()) dir = QDir::tempPath();
        QDir().mkpath(dir);
        const QString path = dir + "/clipboard-"
            + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png";
        if (img.save(path, "PNG")) {
            addWebShared({ QUrl::fromLocalFile(path) });
            return;
        }
    }
#endif
    setWebText(clipboardText());
}

void TransferManager::clearWebShared()
{
    m_webShared.clear();
    m_webText.clear();
    if (m_web)
        QMetaObject::invokeMethod(m_web, "setSharedText", Q_ARG(QString, QString()));
    if (m_web)
        QMetaObject::invokeMethod(m_web, "setSharedFiles", Q_ARG(QStringList, m_webShared));
    emit webChanged();
}

// Un navegador terminó de subir: tratar el resultado igual que una recepción
// normal (exportar en Android y mostrarlo en "Recibidos").
void TransferManager::onWebUploadFinished(const QStringList &paths, const QStringList &rels,
                                           const QString &text)
{
    m_webUploadDone = true;
    m_webLastNotifPct = -1;
    AndroidNotify::stop();

    // Si la subida termina (o se cancela por el otro lado) mientras aún mostramos
    // el diálogo de aceptar/rechazar, quitarlo.
    onIncomingResolved();

    const bool ok = !paths.isEmpty() || !text.isEmpty();
    // Asegurar que la ProgressBar de QML quede al 100% y actualizar estado.
    if (m_state == "receiving" || ok) {
        m_progress = 1.0;
        emit progressChanged();
        setState(ok ? "done" : "error");
    }
    // Resultado persistente (ProgressBanner.showFileNames).
    if (ok) {
        QString summary;
        if (!text.isEmpty()) summary = tr("Texto recibido");
        else {
            bool isFolder = false;
            for (const QString &r : rels) if (r.indexOf('/') > 0) { isFolder = true; break; }
            if (isFolder) summary = QFileInfo(rels.first().left(rels.first().indexOf('/'))).fileName();
            else if (paths.size() > 1) summary = tr("%1 elementos").arg(paths.size());
            else summary = QFileInfo(paths.first()).fileName();
        }
        // Avisa al handler general como si viniera de la LAN (misma UX).
        setLastResult(QStringLiteral("received"), summary);
    }

    // Texto escrito en la página → mismo camino que un texto recibido por LAN.
    if (!text.isEmpty()) {
        emit textReceived(tr("Navegador"), text);
        AndroidNotify::result(tr("Texto recibido"), text.left(60));
    }
    if (paths.isEmpty())
        return;

    // ¿Vino una carpeta? (alguna ruta relativa tiene subdirectorios)
    QString rootName;
    bool isFolder = false;
    for (const QString &r : rels) {
        const int slash = r.indexOf('/');
        if (slash > 0) { isFolder = true; rootName = r.left(slash); break; }
    }

#ifdef Q_OS_ANDROID
    // Exportar cada archivo a la carpeta final y borrar la copia de paso.
    QString firstUri;
    for (int i = 0; i < paths.size(); ++i) {
        const QString rel = rels.at(i);
        const QString savedUri = m_downloadDir.startsWith(QLatin1String("content://"))
            ? AndroidStorage::saveToTree(m_downloadDir, rel, paths.at(i))
            : AndroidStorage::saveToDownloads(paths.at(i), "Vorlan/" + rel);
        if (!savedUri.isEmpty()) {
            QFile::remove(paths.at(i));
            // Eliminar directorios vacíos hacia arriba (solo dentro de la caché web)
            QDir d = QFileInfo(paths.at(i)).dir();
            const QString webSave = webSaveDir();
            while (d.absolutePath().startsWith(webSave) && d.absolutePath() != webSave) {
                QString name = d.dirName();
                d.cdUp();
                if (!d.rmdir(name)) // falla si no está vacío
                    break;
            }
            if (firstUri.isEmpty()) firstUri = savedUri;
        }
    }
    const QString openTarget = isFolder ? QString() : firstUri;
    QString shown = isFolder ? rootName : AndroidStorage::displayNameOf(firstUri);
    if (shown.isEmpty()) shown = QFileInfo(rels.value(0)).fileName();
#else
    const QString openTarget = isFolder ? downloadsPath() : paths.value(0);
    const QString shown = isFolder ? rootName : QFileInfo(paths.value(0)).fileName();
#endif

    const bool many = paths.size() > 1;
    const QString summary = isFolder ? shown
                          : (many ? tr("%1 elementos").arg(paths.size()) : shown);
    qint64 totalSize = 0;
    for (const QString &p : paths) totalSize += QFileInfo(p).size();
    emit receivedFile(summary, openTarget, isFolder || many, tr("Navegador"),
                      totalSize, QDateTime::currentDateTime());
    AndroidNotify::result(tr("Recepción completa"), summary);
}

// Carpeta donde el servidor web deja lo subido. En Android es una zona de paso
// interna (luego se exporta a la carpeta elegida, igual que al recibir por LAN).
void TransferManager::updateWebUrl()
{
    if (!m_webEnabled || m_webPort == 0) {
        m_webUrl.clear();
        return;
    }
    const QString ip = m_webAddrs.value(m_webAddrIndex);
    const QString scheme = m_webTls ? QStringLiteral("https://") : QStringLiteral("http://");
    const QString host = ip.isEmpty() ? QStringLiteral("127.0.0.1") : ip;
    m_webUrl = scheme + host + QStringLiteral(":%1").arg(m_webPort);
}

void TransferManager::setWebTls(bool v)
{
    if (m_webTls == v) return;
    m_webTls = v;
    m_settings.setWebTls(v);
    // Si el servidor está corriendo, lo reiniciamos para que el cambio surta
    // efecto (TLS/non-TLS se decide en onNewConnection() antes del handshake).
    if (m_webEnabled) {
        quint16 oldPort = m_webPort;
        const bool oldBlock = blockSignals(true);
        setWebEnabled(false);
        setWebEnabled(true);
        blockSignals(oldBlock);

        // Restauramos el puerto temporalmente para evitar que la UI colapse
        // al vaciarse m_webUrl, hasta que el hilo asíncrono confirme el puerto.
        m_webPort = oldPort;
        updateWebUrl();
        emit webChanged();
    } else {
        emit webChanged();
    }
}

// Cambiar la dirección mostrada en el QR (el servidor escucha en TODAS, esto
// solo elige cuál se enseña: útil si hay VPN, Docker o varias redes).
void TransferManager::setWebAddressIndex(int i)
{
    if (i < 0 || i >= m_webAddrs.size() || i == m_webAddrIndex)
        return;
    m_webAddrIndex = i;
    updateWebUrl();
    emit webChanged();
}

QString TransferManager::webSaveDir() const
{
#ifdef Q_OS_ANDROID
    QDir d(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/web");
    d.mkpath(".");
    return d.absolutePath();
#else
    QDir d(downloadsPath());
    d.mkpath(".");
    return d.absolutePath();
#endif
}
