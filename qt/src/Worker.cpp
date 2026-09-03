#include "Worker.h"
#include "Protocol.h"
#include "AndroidStorage.h"
#include "AndroidNotify.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QUrl>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QHostAddress>
#include <QDateTime>
#include <QRegularExpression>
#include <QScopeGuard>
#include <memory>
#include <QDebug>

using namespace Proto;

// Servidor TCP que crea QSslSocket (si hay soporte SSL) para permitir subir a TLS
// tras el handshake (STARTTLS). Sin soporte SSL usa QTcpSocket normal (texto plano).
namespace {
class SslServer : public QTcpServer
{
public:
    using QTcpServer::QTcpServer;
protected:
    void incomingConnection(qintptr handle) override
    {
        QTcpSocket *s = QSslSocket::supportsSsl() ? new QSslSocket(this)
                                                  : new QTcpSocket(this);
        s->setSocketDescriptor(handle);
        addPendingConnection(s);
    }
};

// Certificado/clave autofirmados embebidos (para cifrar el canal; evita la
// interceptación pasiva). Cargados una sola vez.
QSslConfiguration serverSslConfig()
{
    static QSslConfiguration cfg = []() {
        QSslConfiguration c = QSslConfiguration::defaultConfiguration();
        QFile cf(QStringLiteral(":/tls/cert.pem"));
        QFile kf(QStringLiteral(":/tls/key.pem"));
        if (cf.open(QIODevice::ReadOnly) && kf.open(QIODevice::ReadOnly)) {
            c.setLocalCertificate(QSslCertificate(cf.readAll(), QSsl::Pem));
            c.setPrivateKey(QSslKey(kf.readAll(), QSsl::Rsa, QSsl::Pem));
        }
        c.setPeerVerifyMode(QSslSocket::VerifyNone);
        return c;
    }();
    return cfg;
}
} // namespace

Worker::Worker(QString selfId, QString selfName, QObject *parent)
    : ITransport(parent)
    , m_selfId(std::move(selfId))
    , m_selfName(std::move(selfName))
{
}

void Worker::startServer()
{
    if (m_server)
        return;
    m_server = new SslServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &Worker::onNewConnection);
    if (!m_server->listen(QHostAddress::AnyIPv4, kTcpPort)) {
        qWarning() << "[Worker] no se pudo escuchar TCP" << kTcpPort
                   << ":" << m_server->errorString();
        return;
    }
    qInfo() << "[Worker] receptor TCP escuchando en" << kTcpPort;
    emit serverReady(kTcpPort);
}

// ---------------- Recepción ----------------

void Worker::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        handleIncoming(sock);   // bloqueante: solo bloquea el hilo worker
        sock->deleteLater();
    }
}

void Worker::handleIncoming(QTcpSocket *sock)
{
    cancelRequested.store(false); // limpiar cancel previo (si no, el siguiente askUser rechazaría auto)
    sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    // Buffers grandes para GbE: 4 MB (el SO lo capa a su máximo)
    sock->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
    sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);

    QJsonObject hello;
    if (!readMessage(sock, hello) ||
        hello.value("type").toString() != QLatin1String(Msg::Hello)) {
        return;
    }

    const QString senderName = hello.value("senderName").toString();
    const qint64 totalBytes = static_cast<qint64>(hello.value("totalBytes").toDouble());
    const int totalItems = hello.value("totalItems").toInt();
    const int totalFiles = hello.value("totalFiles").toInt();   // archivos+textos (para "X/Y")
    const QString summary = hello.value("summary").toString();
    // Sin tope de GB ni de nº de ítems: solo rechazar cabecera corrupta.
    // El único límite es el espacio en disco del receptor; la RAM no se ve afectada
    // porque el archivo se va escribiendo por trozos de 256 KB (kChunk) directo a disco.
    if (totalItems <= 0 || totalBytes < 0) {
        writeMessage(sock, QJsonObject{{"type", Msg::Reject}, {"reason", "cabecera inválida"}});
        emit finished(false, "received", "Cabecera inválida");
        return;
    }
    emit senderIdentified(senderName);   // aunque se acepte automáticamente

    bool accept = autoAccept.load();
    if (!accept)
        accept = askUser(sock, senderName, summary, totalBytes, totalItems);
    emit incomingResolved();

    if (!accept) {
        writeMessage(sock, QJsonObject{{"type", Msg::Reject}, {"reason", "rechazado por el usuario"}});
        emit finished(false, "received", "Transferencia rechazada");
        return;
    }

    // Cifrado (STARTTLS): si el emisor lo pide y este socket es SSL, subimos a TLS.
    QSslSocket *ssl = qobject_cast<QSslSocket *>(sock);
    const bool doTls = hello.value("tls").toBool() && ssl && QSslSocket::supportsSsl();
    if (!writeMessage(sock, QJsonObject{{"type", Msg::Accept}, {"tls", doTls}})) {
        emit finished(false, "received", tr("El emisor se desconectó"));
        return;
    }
    if (doTls) {
        ssl->setSslConfiguration(serverSslConfig());
        ssl->startServerEncryption();
        if (!ssl->waitForEncrypted(kIoTimeoutMs)) {
            emit finished(false, "received", "Fallo al establecer el cifrado");
            return;
        }
        qInfo() << "[Worker] recepción CIFRADA (TLS)";
    }

    cancelRequested.store(false);
    emit statusChanged("receiving");

    const QString base = baseDir();
    QByteArray buf(kChunk, Qt::Uninitialized);
    qint64 receivedTotal = 0;
    int curFile = 0;
    bool sentFolderFlag = false;
    bool receivedFileOrDir = false;   // ¿algo que no sea solo texto?
    QString lastName = summary;
    QElapsedTimer throttle; throttle.start();

    // Deduplicación: si llega una CARPETA cuyo nombre ya existe, se renombra la raíz
    // ("Carpeta (1)") en vez de mezclar los archivos con la existente. Los archivos
    // sueltos ya se deduplican con uniquePath(). `topNames` recoge los nombres REALES
    // de nivel superior guardados, para mostrarlos en Recibidos (no el nombre del emisor).
    QHash<QString, QString> dirRemap;   // nombre carpeta raíz original → nombre único (una vez)
    QStringList topNames;
    auto noteTop = [&topNames](const QString &n) {
        if (!n.isEmpty() && !topNames.contains(n)) topNames << n;
    };

    for (int i = 0; i < totalItems; ++i) {
        QJsonObject item;
        if (!readMessage(sock, item) ||
            item.value("type").toString() != QLatin1String(Msg::Item)) {
            emit finished(false, "received", "Cabecera de ítem inválida");
            return;
        }
        const QString itype = item.value("itemType").toString();
        QString rel = sanitizeRelPath(item.value("relPath").toString());
        const qint64 size = static_cast<qint64>(item.value("size").toDouble());
        if (size < 0) {
            emit finished(false, "received", "Tamaño de archivo inválido");
            return;
        }
        lastName = QFileInfo(rel).fileName();

        // Si el primer segmento es una CARPETA de nivel superior, deduplicar su nombre
        // una sola vez y reescribir todos los ítems que cuelgan de ella.
        const int slash = rel.indexOf('/');
        const QString top = (slash < 0) ? rel : rel.left(slash);
        const bool topIsDir = (slash >= 0) || (itype == QLatin1String("dir"));
        if (topIsDir) {
            if (!dirRemap.contains(top))
                dirRemap.insert(top, uniqueDirName(base, top));
            const QString mapped = dirRemap.value(top);
            rel = mapped + rel.mid(top.size());   // sustituir solo el primer segmento
            noteTop(mapped);
        }

        const QString destPath = base + "/" + rel;

        if (itype == QLatin1String("dir")) {
            QDir().mkpath(destPath);
            receivedFileOrDir = true;
            if (!sentFolderFlag) { sentFolderFlag = true; emit receivedFolder(); }
            continue;
        }

        ++curFile;   // archivo o texto → cuenta para "X/Y"

        // Texto: se muestra en la app (no se guarda como archivo oculto).
        if (itype == QLatin1String("text")) {
            QByteArray textBuf;
            qint64 received = 0;
            while (received < size) {
                if (cancelRequested.load()) {
                    sock->abort();
                    emit finished(false, "received", "Cancelado");
                    return;
                }
                const qint64 want = qMin<qint64>(kChunk, size - received);
                if (!readExact(sock, buf.data(), want)) {
                    emit finished(false, "received", "Conexión interrumpida");
                    return;
                }
                textBuf.append(buf.constData(), want);
                received += want;
                receivedTotal += want;
            }
            const QString txt = QString::fromUtf8(textBuf);
            qInfo().noquote() << "[Worker] texto recibido:" << txt;
            emit textReceived(senderName, txt);
            emit progress(receivedTotal, totalBytes, lastName, curFile, totalFiles);
            notifyProgress(false, receivedTotal, totalBytes, lastName, curFile, totalFiles);
            continue;
        }

        // file
        QFileInfo(destPath).absoluteDir().mkpath(".");
        const QString finalPath = uniquePath(destPath);
        QFile out(finalPath);
        if (!out.open(QIODevice::WriteOnly)) {
            emit finished(false, "received", "No se pudo escribir: " + finalPath);
            return;
        }
        qint64 received = 0;
        while (received < size) {
            if (cancelRequested.load()) {
                out.close(); out.remove();
                sock->abort();
                emit finished(false, "received", "Cancelado");
                return;
            }
            const qint64 want = qMin<qint64>(kChunk, size - received);
            if (!readExact(sock, buf.data(), want)) {
                out.close(); out.remove();
                emit finished(false, "received", "Conexión interrumpida");
                return;
            }
            if (out.write(buf.constData(), want) != want) {
                out.close(); out.remove();
                emit finished(false, "received", "Error de escritura en disco");
                return;
            }
            received += want;
            receivedTotal += want;
            if (throttle.elapsed() >= 64) {
                emit progress(receivedTotal, totalBytes, lastName, curFile, totalFiles);
            notifyProgress(false, receivedTotal, totalBytes, lastName, curFile, totalFiles);
                throttle.restart();
            }
        }
        out.close();
        QString reportPath = finalPath;
        QString shownName = QFileInfo(finalPath).fileName();   // escritorio: nombre real
#ifdef Q_OS_ANDROID
        // Exportar fuera de la zona de paso interna y borrar la copia.
        //  - carpeta elegida por el usuario (SAF, content://…) → guardar ahí.
        //  - por defecto → Descargas/Vorlan pública (visible en Archivos).
        QString ddir;
        { QMutexLocker lock(&m_nameMutex); ddir = m_downloadDir; }
        // Copiar el archivo a la carpeta final puede tardar (es otra escritura
        // completa). Avisar para que la barra no parezca congelada al 100%.
        // Guardar un archivo pequeño es instantáneo: avisar solo cuando la copia
        // se va a notar, para que el contador no parpadee en cada pista.
        if (size >= (16LL << 20)) {
            const QString fname = QFileInfo(rel).fileName();
            emit savingFile(fname, curFile, totalFiles);
            const QString count = totalFiles > 1
                ? QStringLiteral("(%1/%2)  ").arg(qMin(curFile, totalFiles)).arg(totalFiles)
                : QString();
            AndroidNotify::update(false, showNames.load()
                                  ? count + QObject::tr("Guardando %1…").arg(fname)
                                  : count + QObject::tr("Guardando…"), -1);
            m_lastNotifPct = -1;   // forzar refresco al volver al progreso
        }
        const QString savedUri = ddir.startsWith(QLatin1String("content://"))
            ? AndroidStorage::saveToTree(ddir, rel, finalPath)
            : AndroidStorage::saveToDownloads(finalPath, "Vorlan/" + rel);
        if (!savedUri.isEmpty()) {
            QFile::remove(finalPath);
            reportPath = savedUri;   // URI content:// para abrirla luego con la app predeterminada
            // MediaStore pudo renumerar el nombre (p.ej. "imagen (1).png"): usar el real.
            const QString real = AndroidStorage::displayNameOf(savedUri);
            if (!real.isEmpty())
                shownName = real;
        }
#endif
        if (!topIsDir)   // archivo suelto → su nombre REAL para Recibidos
            noteTop(shownName);
        receivedFileOrDir = true;
        emit fileReceivedAt(reportPath);
#ifdef Q_OS_ANDROID
        // Terminado el guardado: restaurar la barra de progreso normal.
        emit progress(receivedTotal, totalBytes, lastName, curFile, totalFiles);
            notifyProgress(false, receivedTotal, totalBytes, lastName, curFile, totalFiles);
#endif
    }
    emit progress(receivedTotal, totalBytes, lastName, curFile, totalFiles);
            notifyProgress(false, receivedTotal, totalBytes, lastName, curFile, totalFiles);

    QJsonObject done;
    if (!readMessage(sock, done) ||
        done.value("type").toString() != QLatin1String(Msg::Done)) {
        emit finished(false, "received", "No se recibió DONE");
        return;
    }
    writeMessage(sock, QJsonObject{{"type", Msg::Complete}});
    // "received_text" = solo texto → la UI no lo lista como archivo (ya está
    // como mensaje). Con archivos/carpetas usa "received".
    // Mostrar el nombre REAL guardado (deduplicado) cuando es un único elemento;
    // con varios, mantener el resumen del emisor ("N archivos", etc.).
    const QString shownSummary = (topNames.size() == 1) ? topNames.first() : summary;
    emit finished(true, receivedFileOrDir ? "received" : "received_text", shownSummary);
}

bool Worker::askUser(QTcpSocket *sock, const QString &senderName, const QString &summary, qint64 size, int items)
{
    QMutexLocker lock(&m_decisionMutex);
    m_decision = -1;
    emit incomingRequest(senderName, summary, size, items);
    // Timeout de 120s: si el usuario no acepta ni rechaza, rechazar
    // automáticamente. Evita que el hilo Worker se bloquee para siempre
    // y la app se quede en blanco al forzar cierre.
    static constexpr int kAskTimeoutMs = 120000;
    QElapsedTimer timer; timer.start();
    while (m_decision < 0) {
        m_decisionCond.wait(&m_decisionMutex, 1000);
        if (m_decision >= 0) break;
        if (!sock || sock->state() != QAbstractSocket::ConnectedState) {
            m_decision = 0; // emisor se fue => rechazar silencioso
            break;
        }
        if (cancelRequested.load()) {
            m_decision = 0;
            break;
        }
        if (timer.elapsed() >= kAskTimeoutMs) {
            m_decision = 0; // timeout => rechazar y liberar el hilo
            qWarning() << "[Worker] timeout aceptar/rechazar tras" << kAskTimeoutMs/1000 << "s";
            break;
        }
    }
    return m_decision == 1;
}

void Worker::respondDecision(bool accept)
{
    QMutexLocker lock(&m_decisionMutex);
    m_decision = accept ? 1 : 0;
    m_decisionCond.wakeAll();
}

void Worker::requestCancel()
{
    cancelRequested.store(true);
}

void Worker::setSelfName(const QString &name)
{
    QMutexLocker lock(&m_nameMutex);
    m_selfName = name;
}

void Worker::setDownloadDir(const QString &dir)
{
    QString norm = dir;
    if (norm.startsWith(QLatin1String("file://"))) {
        const QString local = QUrl(norm).toLocalFile();
        if (!local.isEmpty())
            norm = local;
    }
    if (!norm.startsWith(QLatin1String("content://")) && !norm.isEmpty())
        norm = QDir::cleanPath(norm);
    QMutexLocker lock(&m_nameMutex);
    m_downloadDir = norm;
}

QString Worker::selfNameSafe() const
{
    QMutexLocker lock(&m_nameMutex);
    return m_selfName;
}

QString Worker::baseDir() const
{
#ifdef Q_OS_ANDROID
    // En Android el destino final es Descargas pública (MediaStore); aquí solo
    // una zona de paso propia de la app.
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(base + "/Vorlan");
    dir.mkpath(".");
    return dir.absolutePath();
#else
    QString custom;
    { QMutexLocker lock(&m_nameMutex); custom = m_downloadDir; }
    // Normaliza valores viejos "file://..." y limpia el path (respeta cualquier carpeta elegida)
    if (custom.startsWith(QLatin1String("file://"))) {
        const QString local = QUrl(custom).toLocalFile();
        if (!local.isEmpty())
            custom = local;
    }
    if (!custom.isEmpty())
        custom = QDir::cleanPath(custom);
    if (!custom.isEmpty()) {
        QDir dir(custom);
        dir.mkpath(".");
        return dir.absolutePath();
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    QDir dir(base + "/Vorlan");
    dir.mkpath(".");
    return dir.absolutePath();
#endif
}

// Tamaño legible (igual formato que la UI).
static QString humanSizeW(qint64 b)
{
    static const char *u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = double(b); int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    return QString::number(v, 'f', (i == 0 ? 0 : 1)) + " " + u[i];
}

void Worker::notifyProgress(bool sending, qint64 done, qint64 total,
                            const QString &name, int curFile, int totFiles)
{
    const int pct = total > 0 ? int(double(done) / double(total) * 100.0) : 0;
    if (pct == m_lastNotifPct)
        return;                       // solo al cambiar el % entero
    m_lastNotifPct = pct;
    const QString count = totFiles > 1
        ? QStringLiteral("(%1/%2)  ").arg(qMin(curFile, totFiles)).arg(totFiles)
        : QString();
    const QString text = showNames.load()
        ? QString("%1%2  ·  %3 / %4").arg(count, name, humanSizeW(done), humanSizeW(total))
        : QString("%1%2 / %3").arg(count, humanSizeW(done), humanSizeW(total));
    AndroidNotify::update(sending, text, pct);
}

static QString sanitizeComponent(const QString &raw)
{
    // Usa la misma lógica que WebServer::safeName: quita dirs, reemplaza
    // caracteres peligrosos y evita nombres vacíos. Funciona en Linux/Win/macOS/Android.
    QString base = QFileInfo(raw).fileName();
    if (base.isEmpty()) base = raw;
    // Reemplaza \ / : * ? " < > | y control chars
    static const QRegularExpression re(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    base.replace(re, QStringLiteral("_"));
    base = base.trimmed();
    // Quita puntos finales/espacios (Windows no los permite) y limita longitud
    while (base.endsWith('.') || base.endsWith(' ')) base.chop(1);
    if (base.isEmpty() || base == QLatin1String(".") || base == QLatin1String(".."))
        base = QStringLiteral("archivo-%1").arg(QDateTime::currentMSecsSinceEpoch() % 100000);
    if (base.size() > 200) base = base.left(200);
    return base;
}

QString Worker::sanitizeRelPath(const QString &relPath)
{
    // Previene path traversal y caracteres peligrosos por plataforma.
    QString normalized = relPath;
    normalized.replace('\\', '/'); // normaliza separador Windows
    const QStringList parts = normalized.split('/', Qt::SkipEmptyParts);
    QStringList safe;
    for (const QString &p : parts) {
        if (p == QLatin1String(".") || p == QLatin1String(".."))
            continue;
        const QString c = sanitizeComponent(p);
        if (!c.isEmpty()) safe << c;
    }
    if (safe.isEmpty())
        return QStringLiteral("archivo");
    return safe.join('/');
}

QString Worker::uniquePath(const QString &desired)
{
    if (!QFile::exists(desired))
        return desired;
    const QFileInfo fi(desired);
    const QString dir = fi.absolutePath();
    const QString stem = fi.completeBaseName();
    const QString suf = fi.suffix().isEmpty() ? QString() : "." + fi.suffix();
    int n = 1;
    QString candidate;
    do {
        candidate = QString("%1/%2 (%3)%4").arg(dir, stem).arg(n++).arg(suf);
    } while (QFile::exists(candidate));
    return candidate;
}

QString Worker::uniqueDirName(const QString &base, const QString &name)
{
    // Comprobamos la carpeta destino (base) Y también la carpeta pública en
    // Android (Downloads/Vorlan), porque los archivos exportados se borran de
    // la caché interna. Si el usuario reenvía una carpeta que ya exportamos,
    // debemos crear "Carpeta (1)" en vez de fusionar contenido.
    QString publicBase;
#ifdef Q_OS_ANDROID
    QString pub = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!pub.isEmpty()) publicBase = pub + "/Vorlan";
#endif

    auto existsBoth = [&](const QString &path) -> bool {
        if (QFileInfo::exists(path))
            return true;
        if (!publicBase.isEmpty() && QFileInfo::exists(publicBase + "/" + QFileInfo(path).fileName()))
            return true;
#ifdef Q_OS_ANDROID
        // Android 13+ gestiona Downloads por MediaStore; QFile no lo detecta.
        if (AndroidStorage::existsInDownloads(QFileInfo(path).fileName()))
            return true;
#endif
        return false;
    };

    if (!existsBoth(base + "/" + name))
        return name;
    int n = 1;
    QString cand;
    do {
        cand = QString("%1 (%2)").arg(name).arg(n++);
    } while (existsBoth(base + "/" + cand));
    return cand;
}

// ---------------- Envío ----------------

QList<Worker::SendItem> Worker::buildItems(const QStringList &paths, const QString &text,
                                           qint64 &totalBytes, QString &summary) const
{
    QList<SendItem> items;
    totalBytes = 0;
    int fileCount = 0, dirCount = 0;
    QString firstName;

#ifdef Q_OS_ANDROID
    // Limpiar copias de envíos anteriores antes de resolver content:// de este envío.
    AndroidStorage::clearOutgoingCache();
#endif

    for (const QString &rawPath : paths) {
        QString path = rawPath;
#ifdef Q_OS_ANDROID
        // Qt (FileDialog/FolderDialog) da URIs content:// que no son rutas del sistema.
        // Copiar el archivo/carpeta a la caché y usar la ruta real (así se envía normal).
        if (path.startsWith(QLatin1String("content://"))) {
            path = AndroidStorage::contentToCache(path);
            if (path.isEmpty())
                continue;   // no se pudo resolver → omitir (mejor que mandar "content:")
        }
#endif
        QFileInfo fi(path);
        if (firstName.isEmpty())
            firstName = fi.fileName();
        if (fi.isDir()) {
            ++dirCount;
            const QString parent = fi.dir().absolutePath();   // para incluir el nombre de la carpeta
            // La propia carpeta raíz (por si está vacía)
            SendItem root;
            root.type = "dir";
            root.relPath = QDir(parent).relativeFilePath(fi.absoluteFilePath());
            items << root;
            QDirIterator it(fi.absoluteFilePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo e = it.fileInfo();
                SendItem si;
                si.relPath = QDir(parent).relativeFilePath(e.absoluteFilePath());
                if (e.isDir()) {
                    si.type = "dir";
                } else {
                    si.type = "file";
                    si.absPath = e.absoluteFilePath();
                    si.size = e.size();
                    totalBytes += si.size;
                    ++fileCount;
                }
                items << si;
            }
        } else if (fi.isFile()) {
            SendItem si;
            si.type = "file";
            si.relPath = fi.fileName();
            si.absPath = fi.absoluteFilePath();
            si.size = fi.size();
            totalBytes += si.size;
            ++fileCount;
            items << si;
        }
    }

    if (!text.isEmpty()) {
        SendItem si;
        si.type = "text";
        si.inlineData = text.toUtf8();
        si.size = si.inlineData.size();
        si.relPath = "texto-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".txt";
        totalBytes += si.size;
        items << si;
        if (firstName.isEmpty())
            firstName = "texto";
    }

    // Resumen legible para el receptor
    const int totalNamed = fileCount + dirCount + (text.isEmpty() ? 0 : 1);
    if (totalNamed <= 1) {
        summary = firstName;
    } else {
        summary = QString("%1 y %2 elemento(s) más").arg(firstName).arg(totalNamed - 1);
    }
    return items;
}

void Worker::sendItems(const QString &host, quint16 port,
                       const QStringList &paths, const QString &text)
{
    qint64 totalBytes = 0;
    QString summary;
    const QList<SendItem> items = buildItems(paths, text, totalBytes, summary);
    if (items.isEmpty()) {
        emit finished(false, "sent", "Nada que enviar");
        return;
    }

    const bool wantTls = encrypt.load() && QSslSocket::supportsSsl();
    std::unique_ptr<QTcpSocket> sockHolder(
        wantTls ? static_cast<QTcpSocket *>(new QSslSocket) : new QTcpSocket);
    QTcpSocket &sock = *sockHolder;
    // Registrar el socket activo para poder abortarlo desde requestCancel().
    {
        QMutexLocker lock(&m_sendSockMutex);
        m_activeSendSock = &sock;
    }
    auto clearActiveSock = qScopeGuard([this]() {
        QMutexLocker lock(&m_sendSockMutex);
        m_activeSendSock = nullptr;
    });
    if (wantTls)
        static_cast<QSslSocket &>(sock).setPeerVerifyMode(QSslSocket::VerifyNone);
    sock.connectToHost(QHostAddress(host), port);
    {
        const int kConnPollMs = 500;
        while (sock.state() == QAbstractSocket::ConnectingState) {
            if (cancelRequested.load()) {
                sock.abort();
                emit finished(false, "sent", "Cancelado");
                return;
            }
            if (!sock.waitForConnected(kConnPollMs))
                break;
        }
    }
    if (sock.state() != QAbstractSocket::ConnectedState) {
        emit finished(false, "sent", "No se pudo conectar a " + host + ": " + sock.errorString());
        return;
    }
    sock.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    sock.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
    sock.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);
    cancelRequested.store(false);
    emit statusChanged("sending");

    int totFiles = 0;   // archivos + textos (no carpetas) → para el contador "X/Y"
    for (const SendItem &si : items)
        if (si.type != QLatin1String("dir")) ++totFiles;

    QJsonObject hello{
        {"v", kVersion},
        {"type", Msg::Hello},
        {"senderId", m_selfId},
        {"senderName", selfNameSafe()},
        {"totalItems", int(items.size())},
        {"totalFiles", totFiles},
        {"totalBytes", double(totalBytes)},
        {"summary", summary},
        {"tls", wantTls},
    };
    if (!writeMessage(&sock, hello)) {
        emit finished(false, "sent", "Fallo enviando HELLO");
        return;
    }

    // Esperar respuesta del receptor con posibilidad de cancelar.
    // El receptor tiene ~120s de timeout en askUser; reducimos la espera
    // del emisor para que no bloquee el hilo Worker (que también acepta
    // conexiones entrantes) y para que el usuario pueda cancelar.
    QJsonObject reply;
    {
        const int kPollMs = 2000;      // intervalo de cancelación
        const qint64 kMaxWaitMs = 180000;  // 3 min máximo (receptor auto-rechaza a ~120s)
        QElapsedTimer timer; timer.start();
        bool gotReply = false;
        while (timer.elapsed() < kMaxWaitMs) {
            if (cancelRequested.load()) {
                sock.abort();
                emit finished(false, "sent", "Cancelado");
                return;
            }
            if (readMessage(&sock, reply, kPollMs)) {
                gotReply = true;
                break;
            }
            if (sock.state() != QAbstractSocket::ConnectedState)
                break;
        }
        if (!gotReply) {
            emit finished(false, "sent",
                          sock.state() != QAbstractSocket::ConnectedState
                              ? "Conexión interrumpida"
                              : "Sin respuesta del receptor");
            return;
        }
    }
    if (reply.value("type").toString() != QLatin1String(Msg::Accept)) {
        emit finished(false, "sent", "Rechazado: " + reply.value("reason").toString());
        return;
    }
    // Cifrado: si lo pedimos pero el receptor no lo admite, abortar (no enviar en claro).
    const bool tlsAgreed = reply.value("tls").toBool();
    if (wantTls && !tlsAgreed) {
        emit finished(false, "sent", "El otro dispositivo no admite cifrado");
        return;
    }
    if (tlsAgreed) {
        QSslSocket &s = static_cast<QSslSocket &>(sock);
        s.startClientEncryption();
        if (!s.waitForEncrypted(kIoTimeoutMs)) {
            emit finished(false, "sent", "Fallo al establecer el cifrado: " + s.errorString());
            return;
        }
        qInfo() << "[Worker] envío CIFRADO (TLS)";
    }

    QByteArray buf(kChunk, Qt::Uninitialized);
    qint64 sentTotal = 0;
    int curFile = 0;
    QElapsedTimer throttle; throttle.start();

    for (const SendItem &si : items) {
        QJsonObject header{
            {"type", Msg::Item},
            {"itemType", si.type},
            {"relPath", si.relPath},
            {"size", double(si.size)},
        };
        if (!writeMessage(&sock, header)) {
            emit finished(false, "sent", "Fallo enviando cabecera");
            return;
        }
        if (si.type == QLatin1String("dir"))
            continue;

        const QString name = QFileInfo(si.relPath).fileName();
        ++curFile;

        if (si.type == QLatin1String("text")) {
            if (!writeRaw(&sock, si.inlineData.constData(), si.inlineData.size())) {
                emit finished(false, "sent", "Fallo enviando texto");
                return;
            }
            sentTotal += si.size;
            emit progress(sentTotal, totalBytes, name, curFile, totFiles);
                notifyProgress(true, sentTotal, totalBytes, name, curFile, totFiles);
            continue;
        }

        // file
        QFile file(si.absPath);
        if (!file.open(QIODevice::ReadOnly)) {
            emit finished(false, "sent", "No se pudo abrir " + si.absPath);
            return;
        }
        qint64 sent = 0;
        while (sent < si.size) {
            if (cancelRequested.load()) {
                sock.abort();
                emit finished(false, "sent", "Cancelado");
                return;
            }
            const qint64 n = file.read(buf.data(), kChunk);
            if (n <= 0) {
                emit finished(false, "sent", "Error leyendo " + name);
                return;
            }
            if (!writeRaw(&sock, buf.constData(), n)) {
                emit finished(false, "sent", "Conexión interrumpida");
                return;
            }
            sent += n;
            sentTotal += n;
            if (throttle.elapsed() >= 64) {
                emit progress(sentTotal, totalBytes, name, curFile, totFiles);
                notifyProgress(true, sentTotal, totalBytes, name, curFile, totFiles);
                throttle.restart();
            }
        }
        file.close();
    }
    emit progress(sentTotal, totalBytes, QFileInfo(items.last().relPath).fileName(),
                  totFiles, totFiles);

    if (!writeMessage(&sock, QJsonObject{{"type", Msg::Done}})) {
        emit finished(false, "sent", "Fallo enviando DONE");
        return;
    }
    QJsonObject complete;
    if (!readMessage(&sock, complete) ||
        complete.value("type").toString() != QLatin1String(Msg::Complete)) {
        emit finished(false, "sent", "El receptor no confirmó la recepción");
        return;
    }
    sock.disconnectFromHost();
    emit finished(true, "sent", summary);
}
