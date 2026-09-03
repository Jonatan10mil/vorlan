#pragma once

#include <QObject>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QList>
#include <QDateTime>

#include "SettingsStore.h"

class ITransport;
class SendQueueModel;
class QAbstractItemModel;
class QTimer;
class QNetworkAccessManager;

// Fachada para QML. Dueña del hilo de red y del Worker.
// Expone estado, progreso y el ajuste "aceptar automáticamente".
class TransferManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool autoAccept READ autoAccept WRITE setAutoAccept NOTIFY autoAcceptChanged)
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeChanged)     // "dark"|"light"|"system"
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)      // ""=sistema, "es", "en"…
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY systemDarkChanged)                    // tema oscuro del SO
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentChanged)
    Q_PROPERTY(bool showFileNames READ showFileNames WRITE setShowFileNames NOTIFY showFileNamesChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY notificationsEnabledChanged)
    Q_PROPERTY(bool batteryExempt READ batteryExempt NOTIFY batteryExemptChanged)   // excluida del optimizador (Android)
    Q_PROPERTY(bool discoverable READ discoverable WRITE setDiscoverable NOTIFY discoverableChanged)  // visible para otros
    Q_PROPERTY(bool encrypt READ encrypt WRITE setEncrypt NOTIFY encryptChanged)   // cifrar envíos (TLS)
    Q_PROPERTY(bool sslAvailable READ sslAvailable CONSTANT)                       // ¿hay soporte TLS?
    Q_PROPERTY(QString downloadsFolder READ downloadsPath NOTIFY downloadDirChanged)
    Q_PROPERTY(QString avatarImage READ avatarImage NOTIFY avatarImageChanged)
    Q_PROPERTY(QString localIp READ localIp NOTIFY localIpChanged)
    Q_PROPERTY(QString statusName READ statusName NOTIFY progressChanged)
    Q_PROPERTY(QString statusSize READ statusSize NOTIFY progressChanged)
    Q_PROPERTY(QString statusCount READ statusCount NOTIFY progressChanged)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(bool isTv READ isTv CONSTANT)   // dispositivo Android TV
    // --- Modo web: recibir/enviar desde un navegador, sin instalar VorLAN ---
    Q_PROPERTY(bool webEnabled READ webEnabled WRITE setWebEnabled NOTIFY webChanged)
    Q_PROPERTY(QString webUrl READ webUrl NOTIFY webChanged)
    Q_PROPERTY(int webSharedCount READ webSharedCount NOTIFY webChanged)
    Q_PROPERTY(QString webText READ webText NOTIFY webChanged)
    Q_PROPERTY(QString webError READ webError NOTIFY webChanged)
    Q_PROPERTY(bool webPinEnabled READ webPinEnabled NOTIFY webChanged)
    Q_PROPERTY(QString webPin READ webPin NOTIFY webChanged)   // PIN actual (para mostrarlo)
    Q_PROPERTY(bool webTls READ webTls WRITE setWebTls NOTIFY webChanged)    // HTTPS (true) o HTTP (false)
    // Todas las direcciones por las que se puede llegar a este equipo (el
    // servidor escucha en todas); la del QR es la elegida.
    Q_PROPERTY(QStringList webAddresses READ webAddresses NOTIFY webChanged)
    Q_PROPERTY(int webAddressIndex READ webAddressIndex WRITE setWebAddressIndex NOTIFY webChanged)
    Q_PROPERTY(QString deviceType READ deviceType CONSTANT)   // desktop|laptop|phone|tv
    Q_PROPERTY(QAbstractItemModel* sendQueue READ sendQueue CONSTANT)
    // Solicitud entrante (para el diálogo aceptar/rechazar)
    Q_PROPERTY(bool incomingActive READ incomingActive NOTIFY incomingChanged)
    Q_PROPERTY(QString incomingName READ incomingName NOTIFY incomingChanged)
    Q_PROPERTY(QString incomingSummary READ incomingSummary NOTIFY incomingChanged)
    Q_PROPERTY(QString incomingSizeText READ incomingSizeText NOTIFY incomingChanged)
    Q_PROPERTY(int incomingItems READ incomingItems NOTIFY incomingChanged)
    Q_PROPERTY(bool clipboardHasText READ clipboardHasText NOTIFY clipboardChanged)
    // Resultado reciente para que el banner persista un momento al terminar:
    // ""=nada, "received"=recibido, "sent"=enviado, "error"=fallo.
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY lastResultChanged)
    Q_PROPERTY(QString lastResultText READ lastResultText NOTIFY lastResultChanged)
    // Versión instalada (desde CMake) y estado del chequeo manual de actualizaciones.
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool updateChecking READ updateChecking NOTIFY updateCheckChanged)

public:
    explicit TransferManager(QString selfId, QString selfName, QObject *parent = nullptr);
    ~TransferManager() override;

    QString state() const { return m_state; }
    double progress() const { return m_progress; }
    QString statusText() const { return m_statusText; }
    QString statusName() const { return m_statusName; }
    QString statusSize() const { return m_statusSize; }
    QString statusCount() const { return m_statusCount; }
    bool autoAccept() const { return m_autoAccept; }
    void setAutoAccept(bool v);
    QString deviceName() const { return m_deviceName; }
    void setDeviceName(const QString &name);
    bool busy() const { return m_state == "sending" || m_state == "receiving"; }
    QString themeMode() const { return m_themeMode; }
    void setThemeMode(const QString &m);
    QString language() const { return m_language; }
    void setLanguage(const QString &code);
    bool systemDark() const;
    // Tema efectivo (para elegir el icono claro/oscuro): sigue themeMode, o el SO si "system".
    Q_INVOKABLE bool effectiveDark() const {
        return m_themeMode == QLatin1String("light") ? false
             : m_themeMode == QLatin1String("dark")  ? true
             : systemDark();
    }
    QString accentColor() const { return m_accentColor; }
    void setAccentColor(const QString &c);
    bool showFileNames() const { return m_showFileNames; }
    void setShowFileNames(bool v);
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool v);
    bool notificationsEnabled() const { return m_notificationsEnabled; }
    void setNotificationsEnabled(bool v);
    bool batteryExempt() const;
    Q_INVOKABLE void requestBatteryExemption();    // abre el diálogo del sistema para excluir del optimizador
    bool discoverable() const { return m_discoverable; }
    void setDiscoverable(bool v);
    bool encrypt() const { return m_encrypt; }
    void setEncrypt(bool v);
    bool sslAvailable() const;
    Q_INVOKABLE void startBackgroundReceiver();     // deja la app lista para recibir en 2º plano (app activa)
    QString avatarImage() const { return m_avatarImage.isEmpty() ? m_defaultAvatarImage : m_avatarImage; }
    QString avatarThumb() const;   // miniatura base64 para difundir por la red
    Q_INVOKABLE void setAvatarImage(const QUrl &url);
    QString localIp() const { return m_localIp; }
    QString hostName() const;
    bool isTv() const;
    bool webEnabled() const { return m_webEnabled; }
    void setWebEnabled(bool v);
    QString webUrl() const { return m_webUrl; }
    int webSharedCount() const { return m_webShared.size(); }
    QString webText() const { return m_webText; }
    QString webError() const { return m_webError; }
    bool webPinEnabled() const { return m_webPinEnabled; }
    QString webPin() const { return m_webPin; }
    bool webTls() const { return m_webTls; }
    void setWebTls(bool v);
    // Poner/cambiar el PIN del modo web (PIN "" → desactivar protección).
    Q_INVOKABLE void setWebPin(const QString &pin);
    // Activar/desactivar el PIN sin borrar el valor guardado (para recordarlo
    // entre activaciones; si no hay PIN guardado y se activa, devuelve false).
    Q_INVOKABLE bool setWebPinEnabled(bool enabled);
    QStringList webAddresses() const { return m_webAddrs; }
    int webAddressIndex() const { return m_webAddrIndex; }
    void setWebAddressIndex(int i);
    // SVG del código QR (para mostrarlo con Image { source: ... }).
    Q_INVOKABLE QString qrSvgUri(const QString &text) const;
    // Elegir qué archivos ofrece este equipo para descargar desde el navegador.
    Q_INVOKABLE void setWebShared(const QList<QUrl> &urls);
    Q_INVOKABLE void addWebShared(const QList<QUrl> &urls);         // añadir sin reemplazar
    Q_INVOKABLE void addWebSharedPaths(const QStringList &paths);
    // APKs de apps instaladas: se copian con su nombre legible antes de ofrecerlas.
    Q_INVOKABLE void addWebSharedApps(const QStringList &apkPaths, const QStringList &labels);
    Q_INVOKABLE void setWebText(const QString &text);               // texto para copiar
    Q_INVOKABLE void shareClipboardWeb();                             // texto o imagen del portapapeles → web
    Q_INVOKABLE void clearWebShared();
    QString deviceType() const;
    QString webSaveDir() const;   // dónde guardar lo que suban por web
    void updateWebUrl();          // recalcula webUrl con la IP elegida
    QAbstractItemModel *sendQueue() const;

    void setProfileDefaults(const QString &defaultName) { m_defaultName = defaultName; }
    Q_INVOKABLE void resetProfile();   // vuelve al avatar y nombre originales

    bool incomingActive() const { return m_incomingActive; }
    QString incomingName() const { return m_incomingName; }
    QString incomingSummary() const { return m_incomingSummary; }
    QString incomingSizeText() const { return m_incomingSizeText; }
    int incomingItems() const { return m_incomingItems; }

    // API de envío en cola (a varios dispositivos, uno a la vez)
    Q_INVOKABLE void enqueueSend(const QString &host, int port, const QString &name,
                                 const QString &platform, const QList<QUrl> &urls);
    Q_INVOKABLE void enqueueText(const QString &host, int port, const QString &name,
                                 const QString &platform, const QString &text);
    Q_INVOKABLE void enqueueClipboard(const QString &host, int port, const QString &name,
                                      const QString &platform);
    bool clipboardHasText() const;
    Q_INVOKABLE QString clipboardText() const;   // texto actual del portapapeles
    Q_INVOKABLE void setClipboardText(const QString &text);   // copiar texto al portapapeles
    Q_INVOKABLE void refreshClipboard() { emit clipboardChanged(); }   // re-evaluar (p.ej. al abrir "Enviar")
    QString lastResult() const { return m_lastResult; }
    QString lastResultText() const { return m_lastResultText; }
    Q_INVOKABLE void cancelJob(int id);          // cancelar/quitar un envío de la cola
    Q_INVOKABLE void clearFinishedSends();       // limpiar los ya terminados

    // Envoltorios de conveniencia / ganchos de prueba (sin nombre de destino)
    Q_INVOKABLE void sendPaths(const QString &host, int port, const QList<QUrl> &urls);
    Q_INVOKABLE void sendText(const QString &host, int port, const QString &text);
    Q_INVOKABLE void sendFile(const QString &host, int port, const QUrl &fileUrl);
    Q_INVOKABLE void sendClipboard(const QString &host, int port);

    Q_INVOKABLE void respond(bool accept);   // respuesta al diálogo entrante
    Q_INVOKABLE void cancel();               // abortar la transferencia en curso
    Q_INVOKABLE void clearLastResult();      // ocultar banner de resultado
    Q_INVOKABLE void openDownloadsFolder();  // abrir la carpeta de recibidos
    Q_INVOKABLE void openPath(const QString &path);  // abrir archivo (app predeterminada) o carpeta
    Q_INVOKABLE void openContainingFolder(const QString &path);  // abrir la carpeta que contiene el archivo
    Q_INVOKABLE QString savedFolderLabel(const QString &path) const;  // carpeta real donde se guardó
    Q_INVOKABLE void enqueueSendPaths(const QString &host, int port, const QString &name,
                                      const QString &platform, const QStringList &paths); // enviar por RUTAS (compartir)
    // Android: selector de archivos propio con selección MÚLTIPLE (envía al aceptar).
    Q_INVOKABLE void pickAndroidFiles(const QString &host, int port, const QString &name,
                                      const QString &platform);
    Q_INVOKABLE bool shouldPromptFolder() const;   // ¿pedir elegir carpeta en el 1er arranque? (Android)
    Q_INVOKABLE void markFolderPrompted();         // marcar que ya se preguntó
    Q_INVOKABLE void shareFile(const QString &path); // Android: menú "Compartir" del sistema
    Q_INVOKABLE bool isDir(const QString &path) const;   // ¿es una carpeta? (no compartible)
    Q_INVOKABLE QString downloadsPath() const;
    Q_INVOKABLE void setDownloadDir(const QUrl &url);  // elegir carpeta personalizada
    Q_INVOKABLE void pickAndroidFolder();    // selector de carpetas del sistema (Android/SAF)
    Q_INVOKABLE void applyStatusBar(bool appDark);  // color de iconos de la barra (Android)
    Q_INVOKABLE QString takeSharedFiles();   // rutas de archivos compartidos a la app (Android)
    Q_INVOKABLE QString takeSharedText();    // enlace/texto compartido (p.ej. YouTube)
    Q_INVOKABLE bool takeOpenReceived();     // abierto desde la notificación
    Q_INVOKABLE QString installedApps();     // JSON de apps instaladas (Android)
    Q_INVOKABLE void sendApp(const QString &host, int port, const QString &name,
                             const QString &platform, const QString &apkPath, const QString &label);
    Q_INVOKABLE void sendApps(const QString &host, int port, const QString &name,
                              const QString &platform, const QStringList &apkPaths,
                              const QStringList &labels);   // varias apps a la vez
    // Chequeo MANUAL de actualizaciones (GitHub Releases). Solo se ejecuta cuando
    // el usuario lo pide desde "Acerca de"; sin red automática ni telemetría.
    QString appVersion() const;
    bool updateChecking() const { return m_updateChecking; }
    Q_INVOKABLE void checkForUpdates();

signals:
    void androidFilesPicked();   // el selector de archivos devolvió una selección
    void stateChanged();
    void progressChanged();
    void statusTextChanged();
    void autoAcceptChanged();
    void deviceNameChanged();
    void downloadDirChanged();
    void themeChanged();
    void languageChanged();
    void systemDarkChanged();
    void showFileNamesChanged();
    void closeToTrayChanged();
    void notificationsEnabledChanged();
    void batteryExemptChanged();
    void discoverableChanged();
    void encryptChanged();
    void accentChanged();
    void avatarImageChanged();
    void localIpChanged();
    void incomingChanged();
    void clipboardChanged();
    void lastResultChanged();
    // Resultado del chequeo manual de actualizaciones.
    void updateCheckChanged();   // cambió updateChecking (empieza/termina)
    void updateAvailable(const QString &version, const QString &url);   // hay versión nueva
    void updateUpToDate();       // ya está instalada la última
    void updateCheckFailed();    // sin red o respuesta inválida
    // Para el historial en QML: dirección "sent"/"received"
    void transferDone(bool ok, const QString &direction, const QString &summary);
    void textReceived(const QString &sender, const QString &text);
    void incomingRequested(const QString &name, const QString &summary);   // solicitud entrante
    void webChanged();
    void receivedFile(const QString &summary, const QString &path, bool isFolder,
                      const QString &sender, qint64 size,
                      const QDateTime &timestamp);

    // hacia el worker (cola de hilo)
    void requestSendItems(const QString &host, quint16 port,
                          const QStringList &paths, const QString &text);

private slots:
    void onProgress(qint64 done, qint64 total, const QString &name, int curFile, int totFiles);
    void onSavingFile(const QString &name, int curFile, int totFiles);
    void onReceivedFolder() { m_incomingIsFolder = true; }
    void onSenderIdentified(const QString &n) { if (!n.isEmpty()) m_incomingName = n; }
    void onWebUploadFinished(const QStringList &paths, const QStringList &rels,
                             const QString &text);
    void onStatusChanged(const QString &state);
    void onIncomingRequest(const QString &senderName, const QString &summary, qint64 size, int items);
    void onIncomingResolved();
    void onFinished(bool ok, const QString &direction, const QString &summary);
    void onFileReceivedAt(const QString &path);

private:
    void setState(const QString &s);
    void setStatusText(const QString &s);
    void setLastResult(const QString &kind, const QString &text);   // persiste unos segundos
    void doSend(const QString &host, int port, const QStringList &paths, const QString &text);
    void pumpQueue();                         // arranca el siguiente envío en cola
    void scheduleRemoval(int id);             // quita el envío terminado tras unos segundos
    static QString humanSize(qint64 bytes);
    static QString computeSummary(const QStringList &paths, const QString &text);
    static QString computeGeneric(const QStringList &paths, const QString &text);
    static int compareVersions(const QString &a, const QString &b);   // -1|0|1 ("1.02" vs "1.10")

    // Para el servidor web y cierra su hilo de forma ordenada (idempotente).
    void stopWebServer();

    SettingsStore m_settings;                  // persistencia (QSettings)
    QThread m_thread;
    QThread m_webThread;                       // el servidor web vive aparte
    class WebServer *m_web = nullptr;
    bool m_webEnabled = false;
    QString m_webUrl;
    QStringList m_webShared;
    QString m_webText;
    QString m_webError;
    bool m_webPinEnabled = false;
    QString m_webPin;
    bool m_webTls = false;            // HTTPS (true) o HTTP (false)
    QStringList m_webAddrs;     // IPv4 ordenadas: primero las de LAN real
    int m_webAddrIndex = 0;
    quint16 m_webPort = 0;
    ITransport *m_transport = nullptr;         // motor de transferencia (Worker: TCP)
    SendQueueModel *m_queue = nullptr;
    int m_currentJobId = -1;                   // envío en curso (-1 = ninguno)

    QString m_state = "idle";
    double m_progress = 0.0;
    QString m_statusText;
    QString m_statusName;
    QString m_statusSize;
    QString m_statusCount;
    QString m_lastResult;        // "", "received", "sent", "error" (persiste unos segundos)
    QString m_lastResultText;    // resumen mostrado durante ese lapso
    QTimer *m_resultTimer = nullptr;
    bool m_autoAccept = false;
    QString m_deviceName;
    QString m_downloadDir;   // vacío = predeterminada
    QString m_themeMode = "system";   // el constructor lo relee de SettingsStore
    QString m_language;   // ""=sistema
    bool m_showFileNames = true;
    bool m_closeToTray = false;
    bool m_notificationsEnabled = true;
    bool m_discoverable = true;
    bool m_encrypt = false;
    QString m_accentColor = "#57a63a";
    QString m_avatarImage;         // imagen elegida por el usuario ("file://…"); vacío = usar la del SO
    QString m_defaultAvatarImage;  // foto del usuario del sistema operativo
    QString m_defaultName;         // nombre por defecto (para Restablecer)
    QString m_localIp;
    class QTimer *m_ipTimer = nullptr;

    int m_lastNotifPercent = -1;   // para no saturar la notificación de Android
    QList<qint64> m_recentSpeeds;
    quintptr m_pendingWebConn = 0;  // ID de conexi\u00f3n web esperando aceptaci\u00f3n (0 = ninguna)
    QStringList m_receivedPaths;   // rutas de archivos recibidos en la transferencia actual (escritorio)
    bool m_incomingIsFolder = false;   // ¿lo recibido incluye una carpeta?

    bool m_incomingActive = false;
    QString m_incomingName;
    QString m_incomingSummary;
    QString m_incomingSizeText;
    int m_incomingItems = 0;
    int m_webLastNotifPct = -1;   // progreso web para no saturar notificación
    bool m_webUploadDone = false;  // evita que uploadProgress deshaga estado done/error

    // Chequeo manual de actualizaciones (se crea bajo demanda).
    QNetworkAccessManager *m_updateNet = nullptr;
    bool m_updateChecking = false;
};
