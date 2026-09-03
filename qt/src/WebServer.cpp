#include "WebServer.h"
#include "ZipStream.h"
#include <QRandomGenerator>

#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslServer>
#include <QSslConfiguration>
#include <QSslCertificate>
#include "QSslKey"
#include <QFile>
#include <QFileInfo>
#include "AndroidStorage.h"
#include <QDir>
#include <QUrl>
#include <QMimeDatabase>
#include <QDateTime>
#include <QRegularExpression>
#include <QHostAddress>
#include <QPointer>
#include <QTimer>
#include <QStandardPaths>
#include <QDebug>

namespace {

// Carga un fichero entero (cert/key) desde un recurso o una ruta.
static QByteArray loadRaw(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

} // namespace

QByteArray httpDate()
{
    return QDateTime::currentDateTimeUtc().toString(
               QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT'")).toLatin1();
}

QString humanSize(qint64 b)
{
    static const char *u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = double(b); int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    return QString::number(v, 'f', i == 0 ? 0 : 1) + " " + u[i];
}

QString htmlEscape(const QString &s)
{
    QString o = s;
    o.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;");
    return o;
}

// Nombre de archivo seguro (sin rutas ni caracteres problemáticos).
QString safeName(const QString &raw)
{
    QString n = QFileInfo(raw).fileName();          // descarta cualquier ruta
    n.remove(QChar('\0'));
    n.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    n = n.trimmed();
    if (n.isEmpty() || n == "." || n == "..")
        n = QStringLiteral("archivo-%1").arg(QDateTime::currentSecsSinceEpoch());
    return n;
}

// Subclase de QTcpServer que crea QSslSockets directamente desde el descriptor
// de socket crudo, iniciando el handshake TLS de forma asíncrona. Esto permite
// que múltiples conexiones negocien TLS en paralelo sin bloquearse entre sí,
// a diferencia de QSslServer que serializa los handshakes.
class AsyncSslServer : public QTcpServer {
public:
    QSslConfiguration sslConf;
protected:
    void incomingConnection(qintptr handle) override {
        auto *ssl = new QSslSocket;
        if (!ssl->setSocketDescriptor(handle)) {
            delete ssl;
            return;
        }
        ssl->setSslConfiguration(sslConf);
        ssl->ignoreSslErrors();
        addPendingConnection(ssl);
        ssl->startServerEncryption();
    }
};

// ---------------------------------------------------------------- ciclo de vida

WebServer::WebServer(QObject *parent) : QObject(parent)
{
    m_keepalive = new QTimer(this);
    m_keepalive->setInterval(15000);
    connect(m_keepalive, &QTimer::timeout, this, [this]() {
        QList<QTcpSocket *> dead;
        for (QTcpSocket *s : m_sseClients) {
            if (s->state() != QAbstractSocket::ConnectedState) {
                dead << s;
            } else {
                sendSSE(s, QByteArray(), QByteArray());
            }
        }
        for (QTcpSocket *s : dead) {
            m_sseClients.remove(s);
        }
    });
}

WebServer::~WebServer() { stop(); }

void WebServer::setUseTls(bool on) { m_useTls = on; }

void WebServer::start(quint16 port)
{
    if (m_server)
        return;

    if (m_useTls) {
        // Cargar cert + clave desde QRC; validar antes de arrancar el servidor.
        QSslCertificate cert(loadRaw(":/tls/cert.pem"), QSsl::Pem);
        QSslKey key(loadRaw(":/tls/key.pem"), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
        if (cert.isNull() || key.isNull()) {
            emit failed(tr("Certificado TLS no encontrado o inválido"));
            return;
        }
        QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
        conf.setLocalCertificate(cert);
        conf.setPrivateKey(key);
        conf.setPeerVerifyMode(QSslSocket::VerifyNone);

        // AsyncSslServer crea QSslSockets en incomingConnection() e inicia
        // el handshake TLS de forma asíncrona. Múltiples navegadores pueden
        // negociar TLS en paralelo sin bloquearse entre sí.
        auto *srv = new AsyncSslServer;
        srv->setParent(this);
        srv->sslConf = conf;
        m_server = srv;
    } else {
        m_server = new QTcpServer(this);
    }

    connect(m_server, &QTcpServer::newConnection, this, &WebServer::onNewConnection);
    // Si el puerto está ocupado (otra app, o una instancia anterior), probar los
    // siguientes: la dirección se muestra igualmente en pantalla y en el QR.
    bool ok = false;
    for (int i = 0; i < 6 && !ok; ++i)
        ok = m_server->listen(QHostAddress::Any, quint16(port + i));
    if (!ok) {
        const QString err = m_server->errorString();
        delete m_server; m_server = nullptr;
        emit failed(err);
        return;
    }
    port = m_server->serverPort();
    qInfo() << "[Web] escuchando en" << port << (m_useTls ? "(HTTPS)" : "(HTTP)");
    emit started(port);
}

void WebServer::stop()
{
    if (!m_server)
        return;
    m_sseClients.clear();   // limpiar antes de abortar para no iterar stale pointers
    for (auto it = m_conns.begin(); it != m_conns.end(); ++it) {
        QTcpSocket *s = it.key();
        s->disconnect(this);  // cortar señales → this para que onDisconnected
                              // no llame deleteLater() por segunda vez
        if (it->out) { it->out->close(); delete it->out; it->out = nullptr; }
        s->abort();
        s->deleteLater();
    }
    m_conns.clear();
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
    qInfo() << "[Web] detenido";
}

void WebServer::cancelUploads()
{
    // Abortar solo las conexiones que están recibiendo una subida
    for (auto it = m_conns.begin(); it != m_conns.end();) {
        if (it->method == "POST" && it->path.startsWith("/upload")) {
            QTcpSocket *s = it.key();
            // Esto llamará a onDisconnected indirectamente
            s->abort();
            ++it;
        } else {
            ++it;
        }
    }
}

void WebServer::setSaveDir(const QString &dir) { m_saveDir = dir; }
void WebServer::setPublicDir(const QString &dir) { m_publicDir = dir; }
void WebServer::setDownloadDir(const QString &dir) { m_downloadDir = dir; }
void WebServer::setDeviceName(const QString &name) { m_deviceName = name; }
void WebServer::setAccent(const QString &hex) { if (!hex.isEmpty()) m_accent = hex; }
void WebServer::setPin(const QString &pin)
{
    m_pin = pin.trimmed();
    // Nuevo token → las sesiones ya abiertas dejan de ser válidas.
    quint64 r = QRandomGenerator::system()->generate64();
    m_token = QByteArray::number(r, 16) +
              QByteArray::number(QRandomGenerator::system()->generate64(), 16);
    // Cerrar todas las conexiones SSE activas para forzar la re-autenticación.
    // Los clientes conectados antes del cambio de PIN deberán volver a cargar
    // la página e introducir el PIN (si corresponde).
    const auto sseClientsCopy = m_sseClients;
    for (QTcpSocket *s : sseClientsCopy) {
        s->disconnectFromHost();
        m_sseClients.remove(s);
    }
}

// ¿La conexión trae una cookie de sesión válida? Parsea Cookie: correctamente
// separando por ';' y buscando el par vsid=<token> exacto.
bool WebServer::isAuthed(const Conn &c) const
{
    if (m_pin.isEmpty())
        return true;
    const QList<QByteArray> cookies = c.cookie.split(';');
    for (const QByteArray &raw : cookies) {
        const QByteArray kv = raw.trimmed();
        if (!kv.startsWith("vsid=")) continue;
        const QByteArray v = kv.mid(5).trimmed();
        if (v == m_token) return true;
    }
    return false;
}
void WebServer::setSharedFiles(const QStringList &paths)
{
    if (m_shared == paths) return;
    m_shared = paths;
    QByteArray j = "[";
    for (int i = 0; i < m_shared.size(); ++i) {
        QFileInfo fi(m_shared.at(i));
        const bool dir = fi.isDir();
        qint64 sz = fi.size();
        QString nm = fi.fileName();
        if (dir) {
            sz = 0; // Skip expensive scanning. The UI will just show 0 B or can be ignored.
            nm += QStringLiteral(".zip");
        }
        if (i) j += ",";
        j += "{\"i\":" + QByteArray::number(i) +
             ",\"d\":" + QByteArray(dir ? "true" : "false") +
             ",\"n\":\"" + nm.toUtf8().replace('"', "\\\"") +
             "\",\"s\":\"" + (dir ? "Carpeta" : humanSize(sz).toUtf8()) + "\"}";
    }
    j += "]";
    m_cachedListJson = j;
    broadcastList();
}
void WebServer::setSharedText(const QString &text) { m_sharedText = text; broadcastText(); }

// ---------------------------------------------------------------- conexiones

void WebServer::onEncrypted()
{
    // El handshake TLS terminó bien. El readyRead ya está conectado en onNewConnection.
}

void WebServer::onSslErrors(const QList<QSslError> &errors)
{
    // Certificado autofirmado → el navegador lo advertirá, pero nosotros aceptamos
    // el handshake (el usuario verá el aviso de "sitio no seguro").
    auto *s = qobject_cast<QSslSocket *>(sender());
    if (s) {
        s->ignoreSslErrors(errors);
    }
}

void WebServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *s = m_server->nextPendingConnection();
        if (!s) continue;
        s->setParent(this);
        qDebug() << "[Web] New connection from:" << s->peerAddress().toString();

        m_conns.insert(s, Conn{});
        connect(s, &QTcpSocket::readyRead,    this, &WebServer::onReadyRead);
        connect(s, &QTcpSocket::disconnected, this, &WebServer::onDisconnected);

        // Para TLS: ignorar errores de cert autofirmado en cada socket.
        if (auto *ssl = qobject_cast<QSslSocket *>(s)) {
            QPointer<QSslSocket> weak = ssl;
            connect(ssl, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
                    this, [weak](const QList<QSslError> &) { if (weak) weak->ignoreSslErrors(); });
        }

        // Si ya hay datos disponibles (HTTP puro o TLS con datos post-handshake),
        // procesarlos de inmediato.
        if (s->bytesAvailable() > 0)
            processSocket(s);
    }
}

void WebServer::onDisconnected()
{
    auto *s = qobject_cast<QTcpSocket *>(sender());
    if (!s) return;
    m_sseClients.remove(s);
    auto it = m_conns.find(s);
    if (it != m_conns.end()) {
        const bool wasUpload = (it->method == "POST" && it->path.startsWith("/upload"));
        const bool notFinished = (it->stage != Conn::Done && (it->out || it->bodyRead > 0 || !it->donePaths.isEmpty()));
        if (it->out) {
            const QString partial = it->outPath;
            it->out->close(); delete it->out; it->out = nullptr;
            if (!partial.isEmpty()) QFile::remove(partial);
            // Borrar también los archivos ya terminados de esta misma petición incompleta
            for (const QString &p : std::as_const(it->donePaths)) QFile::remove(p);
        }
        if (wasUpload && notFinished) {
            emit uploadFinished({}, {}, "");
        }
        m_conns.erase(it);
    }
    s->deleteLater();
}

void WebServer::onReadyRead()
{
    auto *s = qobject_cast<QTcpSocket *>(sender());
    if (!s) return;
    processSocket(s);
}

void WebServer::processSocket(QTcpSocket *s)
{
    auto it = m_conns.find(s);
    if (it == m_conns.end()) return;
    Conn &c = *it;

    if (c.isWaitingForUser) return; // pausado

    qDebug() << "[Web] onReadyRead bytesAvailable:" << s->bytesAvailable();
    while (s->bytesAvailable() > 0) {
        if (!c.headersDone) {
            c.buf.append(s->readAll());

            // Detectar TLS ClientHello en servidor HTTP plano (byte 0x16).
            if (!m_useTls && !c.buf.isEmpty()
                && static_cast<quint8>(c.buf[0]) == 0x16) {
                s->abort();
                return;
            }

            const int end = c.buf.indexOf("\r\n\r\n");
            if (end < 0) {
                if (c.buf.size() > 64 * 1024) {   // cabeceras absurdas → cortar
                    sendSimple(s, 431, "text/plain", "Cabeceras demasiado grandes");
                    return;
                }
                return;   // esperar más
            }
            const QByteArray head = c.buf.left(end);
            QByteArray rest = c.buf.mid(end + 4);
            c.buf.clear();
            c.headersDone = true;

            const QList<QByteArray> lines = head.split('\n');
            if (lines.isEmpty()) { sendSimple(s, 400, "text/plain", "Peticion invalida"); return; }
            const QList<QByteArray> req = lines.first().trimmed().split(' ');
            if (req.size() < 2) { sendSimple(s, 400, "text/plain", "Peticion invalida"); return; }
            c.method = QString::fromLatin1(req[0]);
            c.path = QString::fromUtf8(QByteArray::fromPercentEncoding(req[1]));
            for (int i = 1; i < lines.size(); ++i) {
                const QByteArray l = lines[i].trimmed();
                const int colon = l.indexOf(':');
                if (colon < 0) continue;
                const QByteArray k = l.left(colon).toLower();
                const QByteArray v = l.mid(colon + 1).trimmed();
                if (k == "content-length") c.contentLength = v.toLongLong();
                else if (k == "accept-language") c.acceptLang = v;
                else if (k == "cookie") c.cookie = v;
                else if (k == "content-type" && v.contains("multipart/form-data")) {
                    const int bi = v.indexOf("boundary=");
                    if (bi >= 0) {
                        QByteArray b = v.mid(bi + 9);
                        if (b.startsWith('"')) b = b.mid(1, b.indexOf('"', 1) - 1);
                        c.boundary = "--" + b;
                    }
                }
            }
            // Validación básica (sin tope de GB): solo rechazar valor corrupto/negativo.
            if (c.contentLength < 0) {
                sendSimple(s, 400, "text/plain; charset=utf-8", "Content-Length invalido");
                return;
            }
            // ---- CHEQUEO ANTICIPADO DE AUTENTICACIÓN ----
            // Las cabeceras (cookie vsid) ya están parseadas. Comprobamos antes
            // de tocar un solo byte del cuerpo para no escribir nada a disco
            // ni consumir datos si la sesión expiró (PIN activado/cambiado).
            if (!m_pin.isEmpty()) {
                const bool isLogin = (c.method == "POST" && c.path.startsWith("/login"));
                const bool isIndex = (c.method == "GET" && (c.path == "/" || c.path.startsWith("/index")));
                if (!isLogin && !isAuthed(c)) {
                    if (isIndex)
                        sendLoginPage(s, c.acceptLang, false);
                    else
                        sendSimple(s, 401, "text/plain; charset=utf-8", "No autorizado");
                    return;   // descartamos el resto del cuerpo sin procesarlo
                }
            }
            if (c.method == "POST" && !c.boundary.isEmpty()) {
                if (c.path == "/upload" && !c.userAsked) {
                    // Para el modo web, intentamos parsear el primer campo (fileCount) 
                    // antes de pausar, para que la UI pueda mostrar "(1/5)" en vez de "Transferencia".
                    // Hacemos un "peek" usando una copia de la conexión para no alterar el estado real
                    // ni abrir archivos prematuramente.
                    c.userAsked = true;
                    c.isWaitingForUser = true;
                    c.pendingBody = rest;
                    
                    Conn temp = c;
                    temp.inPreAuth = true;
                    feedMultipart(s, temp, rest);
                    c.fileCountTotal = temp.fileCountTotal;
                    
                    QString peerAddr = s->peerAddress().toString();
                    if (s->peerAddress().protocol() == QAbstractSocket::IPv6Protocol) {
                        bool ok = false;
                        quint32 ipv4 = s->peerAddress().toIPv4Address(&ok);
                        if (ok) peerAddr = QHostAddress(ipv4).toString();
                    }
                    
                    emit incomingWebRequest(reinterpret_cast<quintptr>(s), peerAddr, c.contentLength, c.fileCountTotal);
                    return; 
                }
                if (!rest.isEmpty()) { c.bodyRead += rest.size(); feedMultipart(s, c, rest); }
                if (c.bodyRead >= c.contentLength || c.stage == Conn::Done) { handleRequest(s, c); return; }
                continue;
            }
            if (c.method == "POST" && c.contentLength > 0) {   // cuerpo pequeño (login)
                if (!rest.isEmpty()) { c.textBuf.append(rest); c.bodyRead += rest.size(); }
                if (c.bodyRead >= c.contentLength) { handleRequest(s, c); return; }
                continue;
            }
            handleRequest(s, c);
            return;
        }
        // Cuerpo restante
        if (c.isWaitingForUser) return; // pausado, dejamos los datos en el buffer TCP
        const QByteArray chunk = s->readAll();
        c.bodyRead += chunk.size();
        if (!c.boundary.isEmpty()) feedMultipart(s, c, chunk);
        else c.textBuf.append(chunk);
        if (c.bodyRead >= c.contentLength || c.stage == Conn::Done) { handleRequest(s, c); return; }
    }
}

void WebServer::respondToWebRequest(quintptr connId, bool accept)
{
    QTcpSocket *s = reinterpret_cast<QTcpSocket *>(connId);
    if (!m_conns.contains(s)) return;  // la conexión ya cerró
    Conn &c = m_conns[s];
    c.isWaitingForUser = false;

    if (!accept) {
        sendSimple(s, 403, "text/plain", "Transferencia rechazada", true);
        c.stage = Conn::Done;
        return;
    }

    // El usuario aceptó. Procesar lo retenido y seguir
    if (!c.pendingBody.isEmpty()) {
        QByteArray rest = c.pendingBody;
        c.pendingBody.clear();
        c.bodyRead += rest.size();
        feedMultipart(s, c, rest);
    }
    processSocket(s); // leer si llegó algo más al socket mientras esperábamos
}

// ---------------------------------------------------------------- multipart

// Parser incremental: escribe directamente a disco y solo retiene un trozo del
// tamaño de la frontera (para no partirla entre lecturas).
void WebServer::feedMultipart(QTcpSocket *s, Conn &c, const QByteArray &chunk)
{
    Q_UNUSED(s)
    if (c.stage == Conn::Done) return;
    QByteArray data = c.pending + chunk;
    c.pending.clear();

    while (true) {
        if (c.stage == Conn::WantPreamble || c.stage == Conn::WantPartHeaders) {
            const int b = data.indexOf(c.boundary);
            if (c.stage == Conn::WantPreamble) {
                if (b < 0) { c.pending = data.right(qMin(data.size(), c.boundary.size())); return; }
                data = data.mid(b + c.boundary.size());
                c.stage = Conn::WantPartHeaders;
            }
            // cabeceras de la parte
            const int hEnd = data.indexOf("\r\n\r\n");
            if (hEnd < 0) { c.pending = data; return; }
            const QByteArray ph = data.left(hEnd);
            data = data.mid(hEnd + 4);
            // nombre del campo del formulario
            c.partName.clear();
            const int ni = ph.indexOf("name=\"");
            if (ni >= 0) {
                const int e = ph.indexOf('"', ni + 6);
                c.partName = QString::fromUtf8(ph.mid(ni + 6, e - ni - 6));
            }
            // ¿es un archivo?
            QString fname;
            const int fi = ph.indexOf("filename=\"");
            if (fi >= 0) {
                const int e = ph.indexOf('"', fi + 10);
                fname = QString::fromUtf8(ph.mid(fi + 10, e - fi - 10));
            }
            if (fname.isEmpty()) {
                // Campo sin archivo. Puede ser:
                //  - "text": mensaje escrito en la pestaña Texto
                //  - "fileCount": número total de archivos (para banner "(X/Y)")
                if (c.partName == QLatin1String("text")) {
                    if (c.inPreAuth) return; // No capturar texto en pre-auth
                    c.capturingText = true;
                    c.textBuf.clear();
                    c.stage = Conn::InFile;      // se lee igual, pero a memoria
                    continue;
                }
                if (c.partName == QLatin1String("fileCount")) {
                    c.capturingText = true;
                    c.textBuf.clear();
                    c.stage = Conn::InFile;
                    continue;
                }
                c.stage = Conn::WantPreamble;
                continue;
            }
            if (c.inPreAuth) return; // STOP: Hemos llegado al primer archivo, ya no hay más metadatos.

            QString dir = m_saveDir;
            if (dir.isEmpty()) { // fallback si la subida llega antes del setSaveDir encolado
                dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
                if (dir.isEmpty()) dir = QDir::homePath();
                dir += "/Vorlan";
                QDir().mkpath(dir);
            }
            // Con <input webkitdirectory> el navegador manda "Carpeta/sub/archivo".
            QString rel = safeRelPath(fname);
            const int slash = rel.indexOf('/');
            if (slash > 0) {
                const QString root = rel.left(slash);
                if (!c.dirRemap.contains(root)) {
                    QString cand = root;
                    bool exists = false;
#ifdef Q_OS_ANDROID
                    // Verificar si la carpeta existe en el DESTINO REAL del usuario.
                    // 1) Si m_downloadDir es SAF URI → usar DocumentsContract.
                    if (!m_downloadDir.isEmpty() && m_downloadDir.startsWith(QLatin1String("content://"))) {
                        exists = AndroidStorage::folderExistsInTree(m_downloadDir, root);
                    }
                    // 2) Si m_downloadDir es ruta local → QFile::exists().
                    if (!exists && !m_downloadDir.isEmpty()
                        && !m_downloadDir.startsWith(QLatin1String("content://"))) {
                        exists = QFile::exists(m_downloadDir + "/" + root)
                              || QDir(m_downloadDir + "/" + root).exists();
                    }
                    // 3) Fallback: ruta pública (Downloads/Vorlan).
                    if (!exists && !m_publicDir.isEmpty())
                        exists = QFile::exists(m_publicDir + "/" + root)
                              || QDir(m_publicDir + "/" + root).exists();
                    // 4) MediaStore (Android 13+).
                    if (!exists)
                        exists = AndroidStorage::existsInDownloads(root);
#else
                    // Desktop: verificar en la carpeta de destino.
                    const QString destDir = m_downloadDir.isEmpty() ? m_publicDir : m_downloadDir;
                    if (!destDir.isEmpty())
                        exists = QFile::exists(destDir + "/" + root)
                              || QDir(destDir + "/" + root).exists();
#endif
                    if (exists)
                        cand = uniqueIn(dir, root);
                    c.dirRemap.insert(root, cand);
                }
                rel = c.dirRemap.value(root) + rel.mid(slash);
                QDir().mkpath(dir + "/" + QFileInfo(rel).path());
                c.outName = rel;
            } else {
                QDir().mkpath(dir);
                c.outName = uniqueIn(dir, rel);
            }
            c.outPath = dir + "/" + c.outName;
            c.out = new QFile(c.outPath);
            if (!c.out->open(QIODevice::WriteOnly)) {
                delete c.out; c.out = nullptr;
                c.stage = Conn::Done;
                return;
            }
            c.stage = Conn::InFile;
        }

        if (c.stage == Conn::InFile) {
            const int b = data.indexOf(c.boundary);
            if (b < 0) {
                // Retener por si la frontera queda partida entre lecturas.
                const int keep = qMin(data.size(), c.boundary.size() + 2);
                if (data.size() > keep) {
                    const QByteArray part = data.left(data.size() - keep);
                    if (c.capturingText) c.textBuf.append(part);
                    else if (c.out) {
                        c.out->write(part);
                        // curFile = terminados + 1 (el en curso); totFiles = total informado.
                        const int cur = c.fileCountDone + 1;
                        const int tot = c.fileCountTotal;
                        emit uploadProgress(c.bodyRead, c.contentLength, c.outName, cur, tot);
                    }
                }
                c.pending = data.right(keep);
                return;
            }
            int end = b;
            if (end >= 2 && data.mid(end - 2, 2) == "\r\n") end -= 2;   // quitar CRLF previo
            if (c.capturingText) c.textBuf.append(data.left(end));
            else if (c.out) c.out->write(data.left(end));
            finishPart(c);
            data = data.mid(b + c.boundary.size());
            if (data.startsWith("--")) { c.stage = Conn::Done; return; }
            c.stage = Conn::WantPartHeaders;
            continue;
        }
        return;
    }
}

void WebServer::finishPart(Conn &c)
{
    if (c.capturingText) {          // era un campo de texto, no un archivo
        c.capturingText = false;
        // ¿Era el campo que informa cuántos archivos se van a subir?
        if (c.partName == QLatin1String("fileCount")) {
            const QString s = QString::fromUtf8(c.textBuf).trimmed();
            bool ok = false;
            const int n = s.toInt(&ok);
            if (ok && n > 0) c.fileCountTotal = n;
            c.textBuf.clear();
        }
        return;
    }
    if (!c.out) return;
    c.out->flush();
    c.out->close();
    delete c.out;
    c.out = nullptr;
    // Un archivo más terminado.
    c.fileCountDone++;
    // Notificar progreso al completar cada archivo (necesario para archivos
    // pequeños que llegan en un solo chunk y no activan uploadProgress).
    if (c.contentLength > 0) {
        const int cur = c.fileCountDone;
        const int tot = c.fileCountTotal;
        emit uploadProgress(c.bodyRead, c.contentLength, c.outName, cur, tot);
    }
    qInfo() << "[Web] recibido" << c.outPath;
    c.donePaths << c.outPath;
    c.doneRels  << c.outName;
    emit fileUploaded(c.outPath, c.outName);
    c.outPath.clear();
    c.outName.clear();
}

// "Carpeta/sub/archivo.txt" → ruta relativa segura. Descarta ".." y absolutos,
// así un navegador no puede escribir fuera de la carpeta de recibidos.
QString WebServer::safeRelPath(const QString &raw) const
{
    QStringList out;
    const QStringList parts = raw.split('/', Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        if (p == "." || p == "..") continue;
        out << safeName(p);
    }
    if (out.isEmpty())
        out << safeName(raw);
    return out.join('/');
}

QString WebServer::uniqueIn(const QString &dir, const QString &name) const
{
    auto nameExists = [&](const QString &n) -> bool {
#ifdef Q_OS_ANDROID
        if (!m_downloadDir.isEmpty() && m_downloadDir.startsWith(QLatin1String("content://"))) {
            if (AndroidStorage::folderExistsInTree(m_downloadDir, n))
                return true;
        }
        if (!m_downloadDir.isEmpty() && !m_downloadDir.startsWith(QLatin1String("content://"))) {
            if (QFile::exists(m_downloadDir + "/" + n) || QDir(m_downloadDir + "/" + n).exists())
                return true;
        }
        if (!m_publicDir.isEmpty() && (QFile::exists(m_publicDir + "/" + n) || QDir(m_publicDir + "/" + n).exists()))
            return true;
        if (AndroidStorage::existsInDownloads(n))
            return true;
#else
        const QString destDir = m_downloadDir.isEmpty() ? m_publicDir : m_downloadDir;
        if (!destDir.isEmpty() && (QFile::exists(destDir + "/" + n) || QDir(destDir + "/" + n).exists()))
            return true;
#endif
        return false;
    };

    if (!nameExists(name))
        return name;
    const int dot = name.lastIndexOf('.');
    const QString stem = dot > 0 ? name.left(dot) : name;
    const QString ext = dot > 0 ? name.mid(dot) : QString();
    for (int i = 1; i < 10000; ++i) {
        const QString cand = QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(ext);
        if (!nameExists(cand))
            return cand;
    }
    return name;
}

// ---------------------------------------------------------------- respuestas

void WebServer::handleRequest(QTcpSocket *s, Conn &c)
{
    // --- Puerta de acceso por PIN ---
    if (!m_pin.isEmpty()) {
        // Enviar el PIN escrito en la página de acceso.
        if (c.method == "POST" && c.path.startsWith("/login")) {
            const QString ip = s->peerAddress().toString();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            auto it = m_pinAttempts.find(ip);
            if (it != m_pinAttempts.end() && it->second > now) {
                const qint64 retry = (it->second - now) / 1000 + 1;
                sendSimple(s, 429, "text/plain; charset=utf-8",
                           QByteArray("Demasiados intentos. Reintenta en ") + QByteArray::number(retry) + " s");
                return;
            }
            if (it != m_pinAttempts.end() && it->second <= now && it->second != 0) {
                m_pinAttempts.remove(ip);
            }
            QString pin;
            const int pi = c.textBuf.indexOf("pin=");
            if (pi >= 0) {
                QByteArray v = c.textBuf.mid(pi + 4);
                const int amp = v.indexOf('&');
                if (amp >= 0) v = v.left(amp);
                pin = QString::fromUtf8(QByteArray::fromPercentEncoding(v.replace('+', ' '))).trimmed();
            }
            c.textBuf.clear();
            if (pin == m_pin) {
                m_pinAttempts.remove(ip);
                // Correcto: cookie de sesión y a la página principal.
                QByteArray h = "HTTP/1.1 302 Found\r\n";
                h += "Location: /\r\n";
                h += "Set-Cookie: vsid=" + m_token + "; Path=/; HttpOnly; SameSite=Lax";
                if (m_useTls) h += "; Secure";
                h += "\r\n";
                h += "Content-Length: 0\r\nConnection: close\r\n\r\n";
                s->write(h); s->flush(); s->disconnectFromHost();
            } else {
                auto &e = m_pinAttempts[ip];
                e.first++;
                if (e.first >= 5) {
                    e.second = now + 60000; // bloquear 60s tras 5 fallos
                    e.first = 0;
                }
                sendLoginPage(s, c.acceptLang, true);   // PIN incorrecto
            }
            return;
        }
        // Sin sesión válida → pedir el PIN (o 401 para recursos/API).
        if (!isAuthed(c)) {
            if (c.method == "GET" && (c.path == "/" || c.path.startsWith("/index")))
                sendLoginPage(s, c.acceptLang, false);
            else
                sendSimple(s, 401, "text/plain; charset=utf-8", "No autorizado");
            return;
        }
    }

    if (c.method == "POST" && c.path.startsWith("/upload")) {
        const QString txt = QString::fromUtf8(c.textBuf).trimmed();
        const bool hasContent = !c.donePaths.isEmpty() || !txt.isEmpty();
        emit uploadFinished(c.donePaths, c.doneRels, txt);
        c.donePaths.clear(); c.doneRels.clear(); c.textBuf.clear(); c.dirRemap.clear();
        if (hasContent)
            sendSimple(s, 200, "application/json", "{\"ok\":true}");
        else
            sendSimple(s, 400, "application/json", "{\"ok\":false,\"error\":\"no files\"}");
        return;
    }
    if (c.path.startsWith("/dl/")) {
        const int idx = c.path.mid(4).toInt();
        if (idx >= 0 && idx < m_shared.size() && QFileInfo(m_shared.at(idx)).isDir())
            sendFolderZip(s, idx);
        else
            sendFile(s, idx);
        return;
    }
    if (c.path.startsWith("/text.txt")) {
        sendSimple(s, 200, "text/plain; charset=utf-8", m_sharedText.toUtf8());
        return;
    }
    if (c.path.startsWith("/list.json")) { sendList(s); return; }
    if (c.path.startsWith("/events")) {
        // Server-Sent Events: conexión persistente para actualizaciones en tiempo real.
        s->write("HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/event-stream\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Connection: keep-alive\r\n"
                 "Access-Control-Allow-Origin: *\r\n"
                 "\r\n");
        m_sseClients.insert(s);
        // Estado inicial: lista + texto
        broadcastList();
        broadcastText();
        return;
    }
    if (c.path == "/" || c.path.startsWith("/index")) { sendPage(s, c.acceptLang); return; }
    sendSimple(s, 404, "text/plain; charset=utf-8", "No encontrado");
}

void WebServer::sendSimple(QTcpSocket *s, int code, const QByteArray &type,
                           const QByteArray &body, bool close)
{
    QByteArray h = "HTTP/1.1 " + QByteArray::number(code) + " OK\r\n";
    h += "Date: " + httpDate() + "\r\n";
    h += "Content-Type: " + type + "\r\n";
    h += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    h += "Cache-Control: no-store\r\n";
    h += close ? "Connection: close\r\n\r\n" : "\r\n";
    s->write(h);
    s->write(body);
    s->flush();
    if (close) s->disconnectFromHost();
}

void WebServer::sendSSE(QTcpSocket *s, const QByteArray &event, const QByteArray &data)
{
    QByteArray frame;
    if (!event.isEmpty())
        frame += "event: " + event + "\n";
    if (data.isEmpty()) {
        frame += ": keepalive\n\n";
    } else {
        // Enviar línea por línea con prefijo "data: "
        const QList<QByteArray> lines = data.split('\n');
        for (const QByteArray &l : lines)
            frame += "data: " + l + "\n";
        frame += "\n";
    }
    s->write(frame);
    s->flush();
}

void WebServer::broadcastList()
{
    for (QTcpSocket *s : m_sseClients)
        sendSSE(s, "list", m_cachedListJson);
}

void WebServer::broadcastText()
{
    for (QTcpSocket *s : m_sseClients)
        sendSSE(s, "text", m_sharedText.toUtf8());
}

void WebServer::sendList(QTcpSocket *s)
{
    sendSimple(s, 200, "application/json; charset=utf-8", m_cachedListJson);
}

// Sirve una carpeta como .zip generado AL VUELO (sin archivo temporal). Como el
// zip va sin comprimir, su tamaño exacto se conoce de antemano y se puede enviar
// Content-Length (barra de progreso real en el navegador).
void WebServer::sendFolderZip(QTcpSocket *s, int index)
{
    if (index < 0 || index >= m_shared.size()) {
        sendSimple(s, 404, "text/plain; charset=utf-8", "No encontrado");
        return;
    }
    const QString path = m_shared.at(index);
    const auto entries = ZipStream::scanFolder(path);
    const qint64 total = ZipStream::totalSize(entries);
    const QString zipName = QFileInfo(path).fileName() + QStringLiteral(".zip");

    QByteArray h = "HTTP/1.1 200 OK\r\n";
    h += "Date: " + httpDate() + "\r\n";
    h += "Content-Type: application/zip\r\n";
    h += "Content-Length: " + QByteArray::number(total) + "\r\n";
    h += "Content-Disposition: attachment; filename=\"" + zipName.toUtf8() + "\"\r\n";
    h += "Connection: close\r\n\r\n";
    s->write(h);

    auto *zip = new ZipStream(entries);
    // Ir enviando al ritmo que el socket vacía (sin acumular en memoria).
    connect(s, &QTcpSocket::bytesWritten, s, [s, zip](qint64) {
        if (zip->atEnd()) { s->disconnectFromHost(); return; }
        if (s->bytesToWrite() < (1 << 20))
            s->write(zip->next());
    });
    connect(s, &QTcpSocket::disconnected, s, [zip]() { delete zip; });
    s->write(zip->next());
}

void WebServer::sendFile(QTcpSocket *s, int index)
{
    if (index < 0 || index >= m_shared.size()) {
        sendSimple(s, 404, "text/plain; charset=utf-8", "No encontrado");
        return;
    }
    const QString path = m_shared.at(index);
    auto *f = new QFile(path);
    if (!f->open(QIODevice::ReadOnly)) {
        delete f;
        sendSimple(s, 404, "text/plain; charset=utf-8", "No se pudo abrir");
        return;
    }
    const QFileInfo fi(path);
    const QByteArray mime = QMimeDatabase().mimeTypeForFile(path).name().toLatin1();
    QByteArray h = "HTTP/1.1 200 OK\r\n";
    h += "Date: " + httpDate() + "\r\n";
    h += "Content-Type: " + mime + "\r\n";
    h += "Content-Length: " + QByteArray::number(fi.size()) + "\r\n";
    h += "Content-Disposition: attachment; filename=\"" + fi.fileName().toUtf8() + "\"\r\n";
    h += "Connection: close\r\n\r\n";
    s->write(h);
    // Enviar por trozos siguiendo el ritmo del socket (sin cargar todo en RAM).
    connect(s, &QTcpSocket::bytesWritten, f, [s, f](qint64) {
        if (f->atEnd()) { s->disconnectFromHost(); return; }
        if (s->bytesToWrite() < 1 << 20)
            s->write(f->read(256 * 1024));
    });
    connect(s, &QTcpSocket::disconnected, f, &QObject::deleteLater);
    s->write(f->read(256 * 1024));
}

// Página servida al navegador: subir (arrastrar o elegir) y descargar.
// Sin dependencias externas: todo el CSS/JS va embebido (funciona sin internet).
// ---------------------------------------------------------------- idiomas web
// La página se sirve en el idioma que pide el navegador (cabecera
// Accept-Language). Mismos idiomas que la app; si no coincide ninguno, español.
namespace {

enum W { WConnected, WSendHere, WFiles, WFolder, WText, WChooseFiles, WDragHere,
         WChooseFolder, WKeepsTree, WPlaceholder, WSendText, WNoFolderSupport,
         WSending, WSent, WSentN, WTextSent, WSendError, WWriteFirst,
         WSharedText, WCopy, WCopied, WSelectCopy, WDownloadFrom, WNothing,
         WDownload, WSessionExpired, WFolderReject, WPasteClipboard,
         WClipboardError, WTextPasted, WClipboardEmpty, WClipboard,
         WClipboardNotSupported, WClipboardFallbackHint, WClipboardHttpHint,
         WPreparing, W1File, WNFiles, WCount };

const QHash<QString, QStringList> &webLangs()
{
    static const QHash<QString, QStringList> t = {
    {"es", {"Conectado a","Enviar a %1","Archivos","Carpeta","Texto",
            "Elegir archivos","o arrástralos aquí","Elegir carpeta","se conserva su estructura",
            "Escribe o pega aquí el texto…","Enviar texto",
            "Este navegador no permite subir carpetas; usa la pestaña Archivos.",
            "Enviando…","Enviado","Enviados %1 archivos","Texto enviado","Error al enviar",
            "Escribe algo primero","Texto compartido","Copiar","Copiado","Selecciona y copia",
            "Descargar de %1","No hay nada compartido.","Descargar",
            "Sesión expirada. Recarga la página.",
            "No se pueden enviar carpetas desde aquí. Usa la pestaña Carpeta.",
            "Pegar del portapapeles","No se pudo leer el portapapeles","Texto pegado",
            "El portapapeles está vacío.","Portapapeles",
            "Este navegador no soporta lectura del portapapeles (usa HTTPS).","Pulsa para seleccionar y usa Ctrl+V (o ⌘+V en Mac)","Toca para elegir una imagen o hacer una foto",
            "Preparando…","1 archivo","%1 archivos"}},
    {"en", {"Connected to","Send to %1","Files","Folder","Text",
            "Choose files","or drag them here","Choose folder","structure is preserved",
            "Type or paste your text here…","Send text",
            "This browser can't upload folders; use the Files tab.",
            "Sending…","Sent","Sent %1 files","Text sent","Send failed",
            "Type something first","Shared text","Copy","Copied","Select and copy",
            "Download from %1","Nothing shared.","Download",
            "Session expired. Reload the page.",
            "Folders can't be sent from here. Use the Folder tab.",
            "Paste from clipboard","Couldn't read clipboard","Text pasted",
            "Clipboard is empty.","Clipboard",
            "This browser doesn't support clipboard reading (use HTTPS).","Tap to select and press Ctrl+V (or ⌘+V on Mac)","Tap to choose an image or take a photo",
            "Preparing…","1 file","%1 files"}},
    {"fr", {"Connecté à","Envoyer vers %1","Fichiers","Dossier","Texte",
            "Choisir des fichiers","ou glissez-les ici","Choisir un dossier","la structure est conservée",
            "Écrivez ou collez le texte ici…","Envoyer le texte",
            "Ce navigateur ne permet pas d'envoyer des dossiers ; utilisez l'onglet Fichiers.",
            "Envoi…","Envoyé","%1 fichiers envoyés","Texte envoyé","Échec de l'envoi",
            "Écrivez quelque chose","Texte partagé","Copier","Copié","Sélectionnez et copiez",
            "Télécharger depuis %1","Rien de partagé.","Télécharger",
            "Session expirée. Rechargez la page.",
            "Les dossiers ne peuvent pas être envoyés depuis ici. Utilisez l'onglet Dossier.",
            "Coller depuis le presse-papiers","Impossible de lire le presse-papiers","Texte collé",
            "Le presse-papiers est vide.","Presse-papiers",
            "Ce navigateur ne lit pas le presse-papiers (utilisez HTTPS).","Appuyez pour sélectionner puis Ctrl+V (ou ⌘+V sur Mac)","Touchez pour choisir une image ou prendre une photo",
            "Préparation…","1 fichier","%1 fichiers"}},
    {"de", {"Verbunden mit","An %1 senden","Dateien","Ordner","Text",
            "Dateien wählen","oder hierher ziehen","Ordner wählen","Struktur bleibt erhalten",
            "Text hier eingeben oder einfügen…","Text senden",
            "Dieser Browser kann keine Ordner hochladen; nutze den Reiter Dateien.",
            "Wird gesendet…","Gesendet","%1 Dateien gesendet","Text gesendet","Senden fehlgeschlagen",
            "Schreibe zuerst etwas","Geteilter Text","Kopieren","Kopiert","Auswählen und kopieren",
            "Von %1 herunterladen","Nichts geteilt.","Herunterladen",
            "Sitzung abgelaufen. Seite neu laden.",
            "Ordner können von hier nicht gesendet werden. Nutze den Ordner-Reiter.",
            "Aus Zwischenablage einfügen","Zwischenablage konnte nicht gelesen werden","Text eingefügt",
            "Zwischenablage ist leer.","Zwischenablage",
            "Dieser Browser kann die Zwischenablage nicht lesen (HTTPS nötig).","Tippen zum Auswählen und Strg+V (oder ⌘+V am Mac)","Tippen, um ein Bild auszuwählen oder ein Foto aufzunehmen",
            "Vorbereitung…","1 Datei","%1 Dateien"}},
    {"pt", {"Conectado a","Enviar para %1","Arquivos","Pasta","Texto",
            "Escolher arquivos","ou arraste-os aqui","Escolher pasta","a estrutura é preservada",
            "Escreva ou cole o texto aqui…","Enviar texto",
            "Este navegador não permite enviar pastas; use a aba Arquivos.",
            "Enviando…","Enviado","%1 arquivos enviados","Texto enviado","Erro ao enviar",
            "Escreva algo primeiro","Texto compartilhado","Copiar","Copiado","Selecione e copie",
            "Baixar de %1","Nada compartilhado.","Baixar",
            "Sessão expirada. Recarregue a página.",
            "Pastas não podem ser enviadas daqui. Use a aba Pasta.",
            "Colar da área de transferência","Não foi possível ler a área de transferência","Texto colado",
            "A área de transferência está vazia.","Área de transferência",
            "Este navegador não lê a área de transferência (use HTTPS).","Toque para selecionar e use Ctrl+V (ou ⌘+V no Mac)","Toque para escolher uma imagem ou tirar uma foto",
            "Preparando…","1 arquivo","%1 arquivos"}},
    {"it", {"Connesso a","Invia a %1","File","Cartella","Testo",
            "Scegli i file","o trascinali qui","Scegli la cartella","la struttura viene mantenuta",
            "Scrivi o incolla qui il testo…","Invia testo",
            "Questo browser non permette di caricare cartelle; usa la scheda File.",
            "Invio…","Inviato","Inviati %1 file","Testo inviato","Errore nell'invio",
            "Scrivi qualcosa prima","Testo condiviso","Copia","Copiato","Seleziona e copia",
            "Scarica da %1","Niente condiviso.","Scarica",
            "Sessione scaduta. Ricarica la pagina.",
            "Le cartelle non possono essere inviate da qui. Usa la scheda Cartella.",
            "Incolla dagli appunti","Impossibile leggere gli appunti","Testo incollato",
            "Gli appunti sono vuoti.","Appunti",
            "Questo browser non supporta la lettura degli appunti (usa HTTPS).","Tocca per selezionare e premi Ctrl+V (o ⌘+V su Mac)","Tocca per scegliere un'immagine o scattare una foto",
            "Preparazione…","1 file","%1 file"}},
    {"ru", {"Подключено к","Отправить на %1","Файлы","Папка","Текст",
            "Выбрать файлы","или перетащите их сюда","Выбрать папку","структура сохраняется",
            "Введите или вставьте текст…","Отправить текст",
            "Этот браузер не умеет загружать папки; используйте вкладку «Файлы».",
            "Отправка…","Отправлено","Отправлено файлов: %1","Текст отправлен","Ошибка отправки",
            "Сначала введите текст","Общий текст","Копировать","Скопировано","Выделите и скопируйте",
            "Скачать с %1","Ничего не выбрано.","Скачать",
            "Сессия истекла. Перезагрузите страницу.",
            "Папки не могут быть отправлены отсюда. Используйте вкладку «Папка».",
            "Вставить из буфера обмена","Не удалось прочитать буфер обмена","Текст вставлен",
            "Буфер обмена пуст.","Буфер обмена",
            "Этот браузер не читает буфер обмена (используйте HTTPS).","Нажмите для выделения и нажмите Ctrl+V (или ⌘+V на Mac)","Нажмите, чтобы выбрать изображение или сделать фото",
            "Подготовка…","1 файл","%1 файлов"}},
    {"ja", {"接続先","%1 に送る","ファイル","フォルダー","テキスト",
            "ファイルを選択","またはここにドラッグ","フォルダーを選択","構成はそのまま保持されます",
            "ここにテキストを入力または貼り付け…","テキストを送信",
            "このブラウザーはフォルダーを送信できません。「ファイル」タブを使ってください。",
            "送信中…","送信しました","%1 件のファイルを送信しました","テキストを送信しました","送信に失敗しました",
            "先に入力してください","共有されたテキスト","コピー","コピーしました","選択してコピー",
            "%1 からダウンロード","共有されているものはありません。","ダウンロード",
            "セッションが期限切れです。ページを再読み込みしてください。",
            "ここからフォルダーは送信できません。「フォルダー」タブを使ってください。",
            "クリップボードから貼り付け","クリップボードを読み取れませんでした","テキストを貼り付けました",
            "クリップボードは空です。","クリップボード",
            "このブラウザーはクリップボードを読めません（HTTPS を使ってください）。","タップして選択し、Ctrl+V（Mac は ⌘+V）を押してください","タップして画像を選ぶか写真を撮影",
            "準備中…","1 ファイル","%1 ファイル"}},
    {"zh", {"已连接到","发送到 %1","文件","文件夹","文本",
            "选择文件","或拖到这里","选择文件夹","保留目录结构",
            "在此输入或粘贴文本…","发送文本",
            "此浏览器不支持上传文件夹，请使用「文件」标签。",
            "正在发送…","已发送","已发送 %1 个文件","文本已发送","发送失败",
            "请先输入内容","共享的文本","复制","已复制","请选择并复制",
            "从 %1 下载","没有共享内容。","下载",
            "会话已过期。请刷新页面。",
            "无法从此处发送文件夹。请使用「文件夹」标签。",
            "从剪贴板粘贴","无法读取剪贴板","文本已粘贴",
            "剪贴板为空。","剪贴板",
            "此浏览器不支持读取剪贴板（请使用 HTTPS）。","点击选中后按 Ctrl+V（Mac 按 ⌘+V）","点击选择图片或拍照",
            "准备中…","1 个文件","%1 个文件"}},
    {"ar", {"متصل بـ","الإرسال إلى %1","ملفات","مجلد","نص",
            "اختر الملفات","أو اسحبها إلى هنا","اختر مجلدًا","يتم الحفاظ على البنية",
            "اكتب أو الصق النص هنا…","إرسال النص",
            "هذا المتصفح لا يدعم رفع المجلدات؛ استخدم تبويب الملفات.",
            "جارٍ الإرسال…","تم الإرسال","تم إرسال %1 من الملفات","تم إرسال النص","فشل الإرسال",
            "اكتب شيئًا أولاً","نص مشترك","نسخ","تم النسخ","حدّد وانسخ",
            "التنزيل من %1","لا يوجد شيء مشارَك.","تنزيل",
            "انتهت صلاحية الجلسة. أعد تحميل الصفحة.",
            "لا يمكن إرسال المجلدات من هنا. استخدم تبويب المجلد.",
            "لصق من الحافظة","تعذرت قراءة الحافظة","تم لصق النص",
            "الحافظة فارغة.","الحافظة",
            "هذا المتصفح لا يدعم قراءة الحافظة (استخدم HTTPS).","اضغط للتحديد ثم Ctrl+V (أو ⌘+V على ماك)","اضغط لاختيار صورة أو التقاط صورة",
            "جارٍ التحضير…","ملف واحد","%1 ملفات"}},
    {"ko", {"연결된 기기","%1(으)로 보내기","파일","폴더","텍스트",
            "파일 선택","또는 여기로 끌어다 놓기","폴더 선택","구조가 그대로 유지됩니다",
            "여기에 텍스트를 입력하거나 붙여넣기…","텍스트 보내기",
            "이 브라우저는 폴더 업로드를 지원하지 않습니다. 파일 탭을 사용하세요.",
            "보내는 중…","보냈습니다","파일 %1개를 보냈습니다","텍스트를 보냈습니다","보내지 못했습니다",
            "먼저 내용을 입력하세요","공유된 텍스트","복사","복사됨","선택 후 복사하세요",
            "%1에서 다운로드","공유된 항목이 없습니다.","다운로드",
            "세션이 만료되었습니다. 페이지를 새로 고치세요.",
            "여기서는 폴더를 보낼 수 없습니다. 폴더 탭을 사용하세요.",
            "클립보드에서 붙여넣기","클립보드를 읽을 수 없습니다","텍스트가 붙여넣어졌습니다",
            "클립보드가 비어 있습니다.","클립보드",
            "이 브라우저는 클립보드 읽기를 지원하지 않습니다(HTTPS 사용).","탭하여 선택하고 Ctrl+V(Mac은 ⌘+V)를 누르세요","이미지를 선택하거나 사진을 찍으려면 누르세요",
            "준비 중…","파일 1개","파일 %1개"}},
    {"hi", {"इससे जुड़ा","%1 पर भेजें","फ़ाइलें","फ़ोल्डर","टेक्स्ट",
            "फ़ाइलें चुनें","या उन्हें यहाँ खींचें","फ़ोल्डर चुनें","संरचना बनी रहती है",
            "यहाँ टेक्स्ट लिखें या चिपकाएँ…","टेक्स्ट भेजें",
            "यह ब्राउज़र फ़ोल्डर अपलोड नहीं कर सकता; फ़ाइलें टैब का उपयोग करें।",
            "भेजा जा रहा है…","भेज दिया","%1 फ़ाइलें भेजी गईं","टेक्स्ट भेज दिया","भेजने में त्रुटि",
            "पहले कुछ लिखें","साझा किया गया टेक्स्ट","कॉपी करें","कॉपी हो गया","चुनें और कॉपी करें",
            "%1 से डाउनलोड करें","कुछ भी साझा नहीं किया गया।","डाउनलोड",
            "सत्र समाप्त हो गया। पेज को रीलोड करें।",
            "यहाँ से फ़ोल्डर नहीं भेजे जा सकते। फ़ोल्डर टैब का उपयोग करें।",
            "क्लिपबोर्ड से पेस्ट करें","क्लिपबोर्ड पढ़ नहीं सका","टेक्स्ट पेस्ट हो गया",
            "क्लिपबोर्ड खाली है।","क्लिपबोर्ड",
            "यह ब्राउज़र क्लिपबोर्ड पढ़ने का समर्थन नहीं करता (HTTPS का उपयोग करें)।","चुनने के लिए टैप करें और Ctrl+V (या Mac पर ⌘+V) दबाएँ","छवि चुनने या फोटो लेने के लिए टैप करें",
            "तैयारी हो रही है…","1 फ़ाइल","%1 फ़ाइलें"}},
    };
    return t;
}

// Elige el idioma según Accept-Language ("es-ES,es;q=0.9,en;q=0.8").
QString pickLang(const QByteArray &accept)
{
    const auto &langs = webLangs();
    const QList<QByteArray> parts = accept.split(',');
    for (const QByteArray &raw : parts) {
        QByteArray code = raw.split(';').value(0).trimmed().toLower();
        if (code.isEmpty()) continue;
        const QString full = QString::fromLatin1(code);
        const QString base = full.section('-', 0, 0);
        if (langs.contains(full)) return full;
        if (langs.contains(base)) return base;
    }
    return QStringLiteral("en");   // idioma no soportado → inglés
}

} // namespace

// Página de acceso cuando hay PIN. Traducida a los mismos idiomas que la app.
// Strings por idioma: [título, indicación, botón Entrar, error PIN incorrecto].
void WebServer::sendLoginPage(QTcpSocket *s, const QByteArray &acceptLang, bool wrong)
{
    static const QHash<QString, QStringList> T = {
    {"es", {"Acceso protegido","Introduce el PIN","Entrar","PIN incorrecto"}},
    {"en", {"Protected access","Enter the PIN","Enter","Wrong PIN"}},
    {"fr", {"Accès protégé","Saisissez le PIN","Entrer","PIN incorrect"}},
    {"de", {"Geschützter Zugriff","PIN eingeben","Los","Falsche PIN"}},
    {"pt", {"Acesso protegido","Digite o PIN","Entrar","PIN incorreto"}},
    {"it", {"Accesso protetto","Inserisci il PIN","Entra","PIN errato"}},
    {"ru", {"Защищённый доступ","Введите PIN","Войти","Неверный PIN"}},
    {"ja", {"保護されたアクセス","PINを入力","入る","PINが違います"}},
    {"zh", {"受保护的访问","输入 PIN","进入","PIN 错误"}},
    {"ar", {"وصول محمي","أدخل الرمز","دخول","رمز غير صحيح"}},
    {"ko", {"보호된 접근","PIN 입력","입장","잘못된 PIN"}},
    {"hi", {"सुरक्षित पहुँच","PIN दर्ज करें","प्रवेश","गलत PIN"}},
    };
    const QString lang = pickLang(acceptLang);
    const QStringList L = T.value(lang, T.value("en"));
    const bool rtl = (lang == QLatin1String("ar"));

    QString html = QStringLiteral(R"HTML(<!doctype html>
<html lang="%1" dir="%2"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>VorLAN</title>
<style>
 :root{--acc:%3;--bg:#eceef1;--card:#fff;--txt:#1a1c1e;--sub:#5f6368;--line:#d3d6d9}
 @media(prefers-color-scheme:dark){:root{--bg:#15171a;--card:#22262a;--txt:#e6e8ea;--sub:#9aa0a6;--line:#2f3438}}
 *{box-sizing:border-box}html,body{margin:0;height:100%%}
 body{font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--txt);
      display:flex;align-items:center;justify-content:center}
 .card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:26px 24px;
       width:min(90vw,320px);text-align:center}
 h1{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;font-size:19px;margin:0 0 4px;font-weight:800;letter-spacing:.5px}
 p{color:var(--sub);font-size:13px;margin:0 0 18px}
 /* Campo del PIN: compacto y centrado; el letter-spacing solo entre cifras
    (padding-left lo compensa para que el texto quede realmente centrado). */
 input{display:block;width:180px;max-width:100%%;margin:0 auto;font-size:20px;letter-spacing:4px;
       text-align:center;padding:12px 12px 12px 16px;border:1px solid var(--line);
       border-radius:12px;background:var(--bg);color:var(--txt)}
 input:focus{outline:none;border-color:var(--acc)}
 button{display:block;width:180px;max-width:100%%;margin:16px auto 0;font:inherit;font-weight:700;
        background:var(--acc);color:#fff;border:0;border-radius:12px;padding:13px;cursor:pointer}
 .err{color:#d84035;font-size:13px;margin-top:10px;min-height:18px}
</style></head><body>
<form class="card" method="POST" action="/login">
  <h1>%4</h1>
  <p>%5</p>
  <input name="pin" type="password" inputmode="numeric" autocomplete="off" autofocus>
  <button type="submit">%6</button>
  <div class="err">%7</div>
</form></body></html>)HTML")
        .arg(lang, rtl ? QStringLiteral("rtl") : QStringLiteral("ltr"), m_accent,
             htmlEscape(L.at(0)), htmlEscape(L.at(1)), htmlEscape(L.at(2)),
             wrong ? htmlEscape(L.at(3)) : QString());
    sendSimple(s, wrong ? 401 : 200, "text/html; charset=utf-8", html.toUtf8());
}


void WebServer::sendPage(QTcpSocket *s, const QByteArray &acceptLang)
{
    const QString lang = pickLang(acceptLang);
    const QStringList L = webLangs().value(lang);
    const bool rtl = (lang == QLatin1String("ar"));
    QString html = QStringLiteral(R"HTML(<!doctype html>
<html lang="{{LANG}}" dir="{{DIR}}"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'%3E%3Crect width='48' height='48' rx='12' fill='%2315171a'/%3E%3Cpath d='M24 4a20 20 0 0 1 20 20 16 16 0 0 1-16 16 12 12 0 0 1-12-12 8 8 0 0 1 8-8 4 4 0 0 1-4 4' fill='none' stroke='{{ACCENC}}' stroke-width='3.5' stroke-linecap='round'/%3E%3Cpath d='M24 44a20 20 0 0 1-20-20 16 16 0 0 1 16-16 12 12 0 0 1 12 12 8 8 0 0 1-8 8 4 4 0 0 1 4-4' fill='none' stroke='{{ACCENC}}' stroke-width='3.5' stroke-linecap='round'/%3E%3C/svg%3E">
<title>VorLAN</title>
<style>
 :root{--acc:{{ACC}};--bg:#eceef1;--card:#fff;--txt:#1a1c1e;--sub:#5f6368;--line:#d3d6d9}
 @media(prefers-color-scheme:dark){:root{--bg:#15171a;--card:#22262a;--txt:#e6e8ea;--sub:#9aa0a6;--line:#2f3438}}
 *{box-sizing:border-box}
 html,body{margin:0;padding:0}
 body{font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--txt)}
 .wrap{max-width:620px;margin:0 auto;padding:18px 14px 48px}
 h1{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;font-size:20px;margin:0 0 2px;letter-spacing:.5px;font-weight:800}
 .sub{color:var(--sub);font-size:13px;margin-bottom:16px}
 .card{background:var(--card);border-radius:16px;padding:16px;margin-bottom:14px;border:1px solid var(--line)}
 h2{font-size:13px;color:var(--sub);font-weight:700;margin:0 0 10px;letter-spacing:.2px}
 /* Pestañas */
 .tabs{display:flex;gap:6px;margin-bottom:12px}
 .tabs button{flex:1;background:transparent;color:var(--sub);border:1px solid var(--line);
              border-radius:10px;padding:9px 4px;font-size:13px;font-weight:600}
 .tabs button.on{background:var(--acc);color:#fff;border-color:var(--acc)}
 .pane{display:none}
 .pane.on{display:block}
 /* Zona de soltar: display:block es imprescindible (label es inline por defecto) */
 .drop{display:block;width:100%;border:2px dashed var(--acc);border-radius:14px;
       padding:22px 14px;text-align:center;background:rgba(87,166,58,.07);cursor:pointer}
 .drop.hot{background:rgba(87,166,58,.18)}
 .drop b{display:block;font-size:16px;margin-bottom:3px;color:var(--txt)}
 .drop span{display:block;color:var(--sub);font-size:13px}
 input[type=file]{display:none}
 textarea{width:100%;min-height:120px;resize:vertical;border:1px solid var(--line);border-radius:12px;
          padding:12px;font:inherit;background:var(--bg);color:var(--txt)}
 button.go{width:100%;margin-top:10px;font:inherit;font-weight:700;background:var(--acc);color:#fff;
           border:0;border-radius:12px;padding:13px;cursor:pointer}
 button.go:disabled{opacity:.45}
 .row{display:flex;align-items:center;gap:12px;padding:11px 0;border-top:1px solid var(--line)}
 .row:first-child{border-top:0}
 .row .n{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
 .row .s{color:var(--sub);font-size:12px;white-space:nowrap}
 a.dl{color:var(--acc);font-weight:700;text-decoration:none;white-space:nowrap}
 .bar{height:6px;background:var(--line);border-radius:4px;overflow:hidden;margin-top:12px}
 .bar>i{display:block;height:100%;width:0;background:var(--acc);transition:width .2s}
 .st{font-size:13px;color:var(--sub);margin-top:8px;min-height:18px}
 .empty{color:var(--sub);font-size:13px}
 .note{color:var(--sub);font-size:12px;margin-top:8px}
</style></head><body><div class="wrap">
<h1>VorLAN</h1>

<div class="card">
  <h2>{{W1}}</h2>
  <div class="tabs">
    <button id="tA" class="on" onclick="tab('A')">{{W2}}</button>
    <button id="tB" onclick="tab('B')">{{W3}}</button>
    <button id="tC" onclick="tab('C')">{{W4}}</button>
  </div>

  <div id="pA" class="pane on">
    <label class="drop" id="drop">
      <input type="file" id="f" multiple>
      <b>{{W5}}</b>
      <span>{{W6}}</span>
    </label>
  </div>

  <div id="pB" class="pane">
    <label class="drop" id="dropDir">
      <input type="file" id="fd" webkitdirectory directory multiple>
      <b>{{W7}}</b>
      <span>{{W8}}</span>
    </label>
    <div class="note" id="dirnote"></div>
  </div>

  <div id="pC" class="pane">
    <textarea id="txt" placeholder="{{W9}}"></textarea>
    <button class="go" id="sendTxt">{{W10}}</button>
  </div>

  <div class="bar"><i id="pb"></i></div>
  <div style="display:flex;justify-content:space-between;align-items:baseline">
    <div class="st" id="st"></div>
    <button id="cx" style="display:none;background:0;border:0;color:var(--acc);font-size:15px;font-weight:700;cursor:pointer;padding:0;margin-top:8px;line-height:1">✕</button>
  </div>
</div>

<div class="card" id="txtCard" style="display:none">
  <h2>{{W18}}</h2>
  <pre id="shared" style="white-space:pre-wrap;word-break:break-word;margin:0 0 10px;font:inherit"></pre>
  <button class="go" id="copyBtn">{{W19}}</button>
</div>

<div class="card">
  <h2>{{W22}}</h2>
  <div id="list"><div class="empty">{{W23}}</div></div>
</div>
</div>
<script>
const $=id=>document.getElementById(id);
function tab(k){
  for(const x of ['A','B','C']){ $('p'+x).classList.toggle('on',x===k); $('t'+x).classList.toggle('on',x===k); }
}
// Aviso si el navegador no admite subir carpetas (iPhone/Safari).
if(!('webkitdirectory' in document.createElement('input')))
  $('dirnote').textContent='{{W11}}';

// Trunca nombres muy largos para el estado visual.
function shortNames(files){
  const names=[];
  for(let i=0; i<Math.min(3, files.length); i++){
    const x = files[i];
    const n=(x.webkitRelativePath||x.name);
    const j=n.lastIndexOf('/');
    names.push(j<0?n:n.slice(j+1));
  }
  if(files.length<=3) return names.join(', ');
  return names.join(', ')+' +'+(files.length-3);
}
function send(fd,statusLabel,doneLabel,namesText){
  $('pb').style.width='0%';
  const base = namesText?(statusLabel+' · '+namesText):statusLabel;
  const doneBase = namesText?(doneLabel+' · '+namesText):doneLabel;
  $('st').textContent=base + ' · {{W12}}...';
  const xhr=new XMLHttpRequest();
  $('cx').style.display='block';
  $('cx').onclick=()=>xhr.abort();
  xhr.open('POST','/upload');
  xhr.upload.onprogress=e=>{ if(e.lengthComputable){
    const p=Math.round(e.loaded*100/e.total);
    $('pb').style.width=p+'%';
    $('st').textContent=base + ' · {{W12}} '+p+'%';
  }};
  xhr.onload=()=>{
    dirUploading=false;
    $('cx').onclick=()=>{ $('st').textContent=''; $('cx').style.display='none'; };
    if(xhr.status>=200&&xhr.status<300){
      $('pb').style.width='100%';
      $('st').textContent=doneBase+' ✓';
      setTimeout(()=>{$('pb').style.width='0'},1500);
    }else if(xhr.status===401){
      $('pb').style.width='0';
      $('st').textContent='{{W16}} · {{W25}}';
    }else{
      $('pb').style.width='0';
      $('st').textContent='{{W16}} (HTTP '+xhr.status+')';
    }
  };
  xhr.onerror=()=>{ dirUploading=false; $('pb').style.width='0'; $('st').textContent='{{W16}}'; $('cx').onclick=()=>{ $('st').textContent=''; $('cx').style.display='none'; }; };
  xhr.onabort=()=>{ dirUploading=false; $('pb').style.width='0'; $('st').textContent=''; $('cx').style.display='none'; };
  xhr.onloadend=()=>{
    dirUploading=false;
    setTimeout(()=>{ if($('cx').style.display!=='none'){$('cx').onclick=()=>{ $('st').textContent=''; $('cx').style.display='none'; };} },0);
  };
  xhr.send(fd);
}
function upload(filesRaw){
  if(!filesRaw||!filesRaw.length)return;
  const files = [];
  for(let i=0; i<filesRaw.length; i++) files.push(filesRaw[i]);
  const namesText=shortNames(files);
  const countLabel = files.length===1?'{{W36}}':('{{W37}}'.replace('%1',files.length));
  const doneLabel = files.length===1?'{{W13}}':('{{W14}}'.replace('%1',files.length));
  const base = namesText?(countLabel+' · '+namesText):countLabel;
  $('pb').style.width='0%';
  $('st').textContent=base + ' · {{W35}}...';
  setTimeout(()=>{
    const fd=new FormData();
    fd.append('fileCount', String(files.length));
    for(const x of files) fd.append('file',x,x._rel||x.webkitRelativePath||x.name);
    send(fd, countLabel, doneLabel, namesText);
  },0);
}
$('f').onchange=e=>{ upload(e.target.files); e.target.value=''; };
$('sendTxt').onclick=()=>{
  const t=$('txt').value.trim();
  if(!t){ $('st').textContent='{{W17}}'; return; }
  const fd=new FormData(); fd.append('text',t);
  send(fd,'{{W15}}','{{W15}}',''); $('txt').value='';
};
// --- Pestaña Archivos: rechazar carpetas ---
const dropF=$('drop');
['dragenter','dragover'].forEach(e=>dropF.addEventListener(e,ev=>{ev.preventDefault();dropF.classList.add('hot')}));
['dragleave','drop'].forEach(e=>dropF.addEventListener(e,ev=>{ev.preventDefault();dropF.classList.remove('hot')}));
dropF.addEventListener('drop',ev=>{
  ev.preventDefault();
  dropF.classList.remove('hot');
  const items=ev.dataTransfer.items;
  if(items&&items.length){
    let hasDirs=false;
    for(let i=0;i<items.length;i++){
      try{const e=items[i].webkitGetAsEntry&&items[i].webkitGetAsEntry();
        if(e&&e.isDirectory){hasDirs=true;break;}}catch(ex){}
    }
    if(hasDirs){$('st').textContent='{{W26}}';return;}
  }
  upload(ev.dataTransfer.files);
});
// --- Pestaña Carpeta: aceptar carpetas por drag-and-drop ---
const dropD=$('dropDir');
['dragenter','dragover'].forEach(e=>dropD.addEventListener(e,ev=>{ev.preventDefault();dropD.classList.add('hot')}));
['dragleave','drop'].forEach(e=>dropD.addEventListener(e,ev=>{ev.preventDefault();dropD.classList.remove('hot')}));
let dirUploading=false;
let dirWatchDog=null;
dropD.addEventListener('drop',ev=>{
  ev.preventDefault();
  dropD.classList.remove('hot');
  if(dirUploading)return;
  $('pb').style.width='0%';
  $('st').textContent='{{W35}}...';
  const items=ev.dataTransfer.items;
  if(!items||!items.length){dirUploading=true;upload(ev.dataTransfer.files);return;}
  const files=[];let pending=0;let anyEntry=false;
  function finish(){
    if(dirWatchDog){clearTimeout(dirWatchDog);dirWatchDog=null;}
    if(dirUploading)return;
    if(files.length){dirUploading=true;upload(files);}
  }
  function check(){if(pending===0)finish();}
  function walk(entry,prefix){
    anyEntry=true;
    if(entry.isFile){
      pending++;
      entry.file(
        f=>{f._rel=prefix?prefix+'/'+f.name:f.name;files.push(f);pending--;check();},
        ()=>{pending--;check();}
      );
    }else if(entry.isDirectory){
      const rd=entry.createReader();
      const dirName=prefix?prefix+'/'+entry.name:entry.name;
      (function readBatch(){
        pending++;
        rd.readEntries(ents=>{
          pending--;
          if(!ents.length){check();return;}
          for(const e of ents)walk(e,dirName);
          readBatch();
        },()=>{pending--;check();return;});
      })();
    }
  }
  for(const it of items){
    try{const e=it.webkitGetAsEntry&&it.webkitGetAsEntry();if(e)walk(e,'');}catch(ex){}
  }
  dirWatchDog=setTimeout(()=>{if(pending===0)finish();},2000);
  check();
  $('fd').value='';
});
$('fd').onchange=e=>{if(!dirUploading){dirUploading=true;upload(e.target.files);}e.target.value='';};
function renderList(l){
  if(!l.length){ $('list').innerHTML='<div class="empty">{{W23}}</div>'; return; }
  $('list').innerHTML=l.map(x=>`<div class="row"><div class="n">${x.d?'📁 ':''}${x.n}</div><div class="s">${x.s}</div><a class="dl" href="/dl/${x.i}" data-name="${x.n.replace(/"/g,'&quot;')}">{{W24}}</a></div>`).join('');
  let dlLock=false;
  document.querySelectorAll('a.dl').forEach(a=>{
    a.onclick=e=>{
      e.preventDefault();
      if(dlLock)return;
      dlLock=true;
      const url=a.href;
      const name=a.dataset.name||'archivo';
      const origText=a.textContent;
      a.style.pointerEvents='none'; a.style.opacity='.55'; a.textContent='…';
      $('st').textContent='↓ '+name+' {{W12}}';
      setTimeout(()=>{
        a.style.pointerEvents=''; a.style.opacity=''; a.textContent=origText;
        if($('st').textContent.startsWith('↓ '+name))$('st').textContent='';
        dlLock=false;
      },3200);
      window.location.href=url;
    };
  });
}
function renderText(t){
  if(!t){ $('txtCard').style.display='none'; return; }
  $('shared').textContent=t; $('txtCard').style.display='';
  $('copyBtn').onclick=()=>{
    navigator.clipboard?.writeText(t).then(()=>{$('copyBtn').textContent='{{W20}} ✓';
      setTimeout(()=>{$('copyBtn').textContent='{{W19}}'},1500);})
    .catch(()=>{ const r=document.createRange(); r.selectNodeContents($('shared'));
      const sel=getSelection(); sel.removeAllRanges(); sel.addRange(r);
      $('copyBtn').textContent='{{W21}}';
    });
  };
}
let sseFailures=0;
function connectSSE(){
  const es=new EventSource('/events');
  es.addEventListener('list',e=>{sseFailures=0;renderList(JSON.parse(e.data));});
  es.addEventListener('text',e=>{sseFailures=0;renderText(e.data);});
  es.onerror=()=>{
    es.close();
    sseFailures++;
    if(sseFailures>=2){
      $('st').textContent='{{W25}}';
    }
    setTimeout(connectSSE,3000);
  };
}
connectSSE();
</script></body></html>)HTML");
    html.replace(QStringLiteral("{{LANG}}"), lang);
    html.replace(QStringLiteral("{{DIR}}"), rtl ? QStringLiteral("rtl") : QStringLiteral("ltr"));
    // Color de acento = el de la app (claro/oscuro lo decide el navegador).
    html.replace(QStringLiteral("{{ACC}}"), m_accent);
    html.replace(QStringLiteral("{{ACCENC}}"), QString(m_accent).replace(QLatin1String("#"), QLatin1String("%23")));
    for (int i = 0; i < L.size(); ++i) {
        QString v = L.at(i);
        // Los encabezados llevan el NOMBRE del equipo: decir "este equipo" se
        // confunde con el dispositivo donde está abierto el navegador.
        if (i == WSendHere || i == WDownloadFrom)
            v = v.arg(m_deviceName.isEmpty() ? QStringLiteral("VorLAN") : m_deviceName);
        html.replace(QStringLiteral("{{W%1}}").arg(i), htmlEscape(v));
    }
    sendSimple(s, 200, "text/html; charset=utf-8", html.toUtf8());
}
