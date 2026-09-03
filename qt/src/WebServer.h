#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QSslError>   // tipo completo requerido por MOC para el slot onSslErrors
#include <QSslConfiguration>

class QTimer;
class QSslSocket;
class QSslConfiguration;

class ZipStream;

class QTcpServer;
class QTcpSocket;
class QFile;

// Servidor HTTP mínimo para "Modo web": permite que CUALQUIER dispositivo con
// navegador (sin instalar VorLAN) suba archivos a este equipo y descargue los
// que este equipo comparta. Vive en su propio hilo para no bloquear la interfaz.
//
// Rutas:
//   GET  /            → página (subir + lista de descargas)
//   POST /upload      → multipart/form-data, se guarda en streaming (sin RAM)
//   GET  /dl/<n>      → descarga el archivo compartido n
//   GET  /list.json   → lista de archivos compartidos (la usa la página)
//   GET  /events      → Server-Sent Events para actualizaciones en tiempo real
class WebServer : public QObject
{
    Q_OBJECT
public:
    explicit WebServer(QObject *parent = nullptr);
    ~WebServer() override;

public slots:
    void start(quint16 port);
    void stop();
    void cancelUploads();
    // Carpeta donde guardar lo que suban (en Android: zona de paso interna).
    void setSaveDir(const QString &dir);
    void setPublicDir(const QString &dir);   // destino final en Android (Downloads/Vorlan)
    void setDownloadDir(const QString &dir); // destino real del usuario (SAF URI o ruta local)
    // Elementos que este equipo ofrece para descargar. Cada uno puede ser un
    // archivo, una carpeta (se sirve como .zip generado al vuelo) o un texto.
    void setSharedFiles(const QStringList &paths);
    // Texto que se muestra en la página para copiar ("" = ninguno).
    void setSharedText(const QString &text);
    // Nombre visible del equipo (se muestra en la página).
    void setDeviceName(const QString &name);
    void setAccent(const QString &hex);   // color de acento de la app
    // PIN de acceso ("" = sin protección). Al cambiarlo caducan las sesiones
    // abiertas (se regenera el token de sesión).
    void setPin(const QString &pin);
    // Activar/desactivar TLS (HTTPS) — solo tiene efecto antes de start().
    void setUseTls(bool on);

signals:
    void started(quint16 port);
    void failed(const QString &error);
    // Señal para pedir permiso al usuario (web a app). connId es el ID de la conexión pendiente.
    void incomingWebRequest(quintptr connId, const QString &senderName, qint64 size, int items);
    // Un archivo terminó de subirse (ruta local ya guardada).
    void fileUploaded(const QString &path, const QString &fileName);
    // Toda la petición terminó: rutas guardadas + sus rutas relativas (con
    // subcarpetas si se subió una carpeta) y el texto escrito, si lo hubo.
    void uploadFinished(const QStringList &paths, const QStringList &relPaths,
                        const QString &text);
    // Progreso de una subida en curso. done/total son bytes del cuerpo multipart.
    // curFile/totFiles son el número de archivos (totFiles = -1 si el cliente
    // no lo informó; entonces solo se muestra el progreso de bytes).
    void uploadProgress(qint64 done, qint64 total, const QString &name,
                        int curFile, int totFiles);

public slots:
    void respondToWebRequest(quintptr connId, bool accept);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onEncrypted();
    void onSslErrors(const QList<QSslError> &errors);

private:
    void processSocket(QTcpSocket *s);
    void processReadyRead(QTcpSocket *s);
    // Estado por conexión (una petición HTTP puede llegar en muchos trozos).
    struct Conn {
        QByteArray buf;             // acumulador de cabeceras
        bool headersDone = false;
        QString method, path;
        qint64 contentLength = 0;
        qint64 bodyRead = 0;
        QByteArray boundary;        // multipart
        // Estado del parser multipart
        enum Stage { WantPreamble, WantPartHeaders, InFile, Done } stage = WantPreamble;
        QFile *out = nullptr;
        QString outName, outPath;
        QByteArray pending;         // trozo retenido (posible inicio de frontera)
        QByteArray acceptLang;      // idioma preferido del navegador
        QByteArray cookie;          // cabecera Cookie (sesión del PIN)
        QString partName;           // nombre del campo del formulario
        bool capturingText = false; // parte "text" (mensaje escrito, sin archivo)
        QByteArray textBuf;
        bool userAsked = false;
        bool isWaitingForUser = false;
        bool inPreAuth = false;
        QByteArray pendingBody;
        QStringList donePaths, doneRels;      // resultados de esta petición
        QHash<QString, QString> dirRemap;     // carpeta raíz → nombre único
        // Contadores para la banner de progreso (lado UI servidor)
        int fileCountTotal = -1;    // -1 = el cliente no lo informó
        int fileCountDone = 0;
        bool justFinishedFile = false;
    };

    void handleRequest(QTcpSocket *s, Conn &c);
    void feedMultipart(QTcpSocket *s, Conn &c, const QByteArray &chunk);
    void finishPart(Conn &c);
    void sendPage(QTcpSocket *s, const QByteArray &acceptLang);
    void sendLoginPage(QTcpSocket *s, const QByteArray &acceptLang, bool wrong);
    bool isAuthed(const Conn &c) const;
    void sendList(QTcpSocket *s);
    void sendFile(QTcpSocket *s, int index);
    void sendFolderZip(QTcpSocket *s, int index);
    void sendSimple(QTcpSocket *s, int code, const QByteArray &type,
                    const QByteArray &body, bool close = true);
    void sendSSE(QTcpSocket *s, const QByteArray &event, const QByteArray &data);
    void broadcastList();
    void broadcastText();
    QString uniqueIn(const QString &dir, const QString &name) const;
    // Convierte "carpeta/sub/archivo.txt" en una ruta relativa segura (sin "..").
    QString safeRelPath(const QString &raw) const;

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Conn> m_conns;
    // Rate-limit PIN: ip -> {fails, blockUntilMs}
    QHash<QString, QPair<int, qint64>> m_pinAttempts;
    QString m_saveDir;
    QString m_publicDir;    // destino final Android (para deduplicar carpetas)
    QString m_downloadDir;  // destino real del usuario (SAF URI o ruta local)
    QString m_deviceName;
    QString m_accent = QStringLiteral("#57a63a");
    QString m_pin;              // PIN de acceso (vacío = sin protección)
    QByteArray m_token;         // token de sesión válido (cookie vsid)
    QStringList m_shared;
    QByteArray m_cachedListJson = "[]";
    QString m_sharedText;
    bool m_useTls = false;      // ¿usar HTTPS en vez de HTTP?

    // SSE: clientes conectados al endpoint /events
    QSet<QTcpSocket *> m_sseClients;
    QTimer *m_keepalive = nullptr;
};
