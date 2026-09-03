#include <QtGlobal>              // define Q_OS_ANDROID antes del #if
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QGuiApplication>
using AppClass = QGuiApplication;
#else
#include <QApplication>            // QSystemTrayIcon necesita QtWidgets
using AppClass = QApplication;
#endif
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QUuid>
#include <QHostInfo>
#include <QTimer>
#include <QUrl>
#include <QIcon>
#include <QTranslator>
#include <QLocale>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QScreen>

#include "Discovery.h"
#include "DeviceListModel.h"
#include "TransferManager.h"
#include "AndroidDevice.h"
#include "AndroidNotify.h"
#include "TrayController.h"

#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
#include <pwd.h>
#include <unistd.h>
#endif

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#include <QLocalServer>
#include <QLocalSocket>
#endif
#include <QWindow>

// Nombre amistoso del usuario para el dispositivo (nombre real > usuario > host).
static QString defaultDeviceName()
{
#ifdef Q_OS_ANDROID
    const QString a = AndroidDevice::deviceName();
    if (!a.isEmpty())
        return a;
    return QStringLiteral("Mi teléfono");
#endif
#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    if (struct passwd *pw = getpwuid(getuid())) {
        // El primer campo del GECOS es el nombre completo.
        QString full = QString::fromLocal8Bit(pw->pw_gecos).section(',', 0, 0).trimmed();
        if (!full.isEmpty())
            return full;
        if (pw->pw_name && *pw->pw_name)
            return QString::fromLocal8Bit(pw->pw_name);
    }
#endif
    QString u = qEnvironmentVariable("USER");
    if (u.isEmpty()) u = qEnvironmentVariable("USERNAME");
    if (u.isEmpty()) u = qEnvironmentVariable("LOGNAME");
    if (!u.isEmpty()) return u;
    QString h = QHostInfo::localHostName();
    if (!h.isEmpty() && h != QLatin1String("UNKNOWN"))
        return h;
    return QStringLiteral("Mi equipo");
}

int main(int argc, char *argv[])
{
    AppClass app(argc, argv);
    app.setApplicationName("Vorlan");
    app.setOrganizationName("Vorlan");        // para QSettings
    app.setOrganizationDomain("vorlan.local");

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // --- Instancia única (escritorio) ---
    // Si ya hay una instancia corriendo, le pedimos que se muestre y salimos, en
    // vez de abrir otra ventana. Clave por usuario para no chocar entre cuentas.
    QString kInstanceKey = QStringLiteral("VorlanSingleInstance-")
#if defined(Q_OS_UNIX)
        + QString::number(static_cast<qulonglong>(getuid()));
#else
        + qEnvironmentVariable("USERNAME");
#endif
    // En pruebas se lanzan varias instancias con VORLAN_ID distintos: que cada una
    // tenga su propia clave para no bloquearse entre sí (los usuarios reales no lo fijan).
    if (qEnvironmentVariableIsSet("VORLAN_ID"))
        kInstanceKey += QLatin1Char('-') + qEnvironmentVariable("VORLAN_ID");
    {
        QLocalSocket probe;
        probe.connectToServer(kInstanceKey);
        if (probe.waitForConnected(300)) {
            probe.write("raise");
            probe.flush();
            probe.waitForBytesWritten(300);
            return 0;   // ya hay otra instancia: activada, salimos
        }
    }
    QLocalServer instanceServer;
    QLocalServer::removeServer(kInstanceKey);   // limpiar socket huérfano si lo hubiera
    instanceServer.listen(kInstanceKey);
#endif

    // --- Identidad persistida (id estable + nombre) ---
    // Limpieza de caché huérfana (celular: 876 MB reportado). Borra restos de
    // envíos/compartidos previos en cache/shared, cache/outgoing y peer-avatars
    // que no se limpiaron (solo debe quedar lo del último envío).
    {
        auto prune = [](const QString &sub, int keepDays) {
            QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
            if (base.isEmpty()) return;
            QDir dir(base + "/" + sub);
            if (!dir.exists()) return;
            const QDateTime now = QDateTime::currentDateTime();
            for (const QFileInfo &fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (fi.lastModified().daysTo(now) >= keepDays) {
                    if (fi.isDir()) QDir(fi.absoluteFilePath()).removeRecursively();
                    else QFile::remove(fi.absoluteFilePath());
                }
            }
        };
        prune("shared", 0);      // ShareIntent: borrar al arrancar (no deja copia)
        prune("outgoing", 0);    // SafStorage: borrar al arrancar
        prune("appicons", 7);
        prune("apks", 1);
        // peer-avatars ya tiene LRU 50 en Discovery.cpp:238
    }
    QSettings settings;
    settings.remove("avatar"); // migracion: borrar clave legacy emoji
    QString deviceId = qEnvironmentVariable("VORLAN_ID");
    if (deviceId.isEmpty())
        deviceId = settings.value("deviceId").toString();
    if (deviceId.isEmpty()) {
        deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("deviceId", deviceId);
    }
    const QString defName = defaultDeviceName();   // nombre del usuario del sistema
    QString deviceName = qEnvironmentVariable("VORLAN_NAME");
    if (deviceName.isEmpty())
        deviceName = settings.value("deviceName").toString();
    if (deviceName.isEmpty())
        deviceName = defName;

    Discovery discovery(deviceId, deviceName);
    DeviceListModel devices;

    QObject::connect(&discovery, &Discovery::peerFound,
                     &devices, &DeviceListModel::onPeerFound);
    QObject::connect(&discovery, &Discovery::peerUpdated,
                     &devices, &DeviceListModel::onPeerUpdated);
    QObject::connect(&discovery, &Discovery::peerLost,
                     &devices, &DeviceListModel::onPeerLost);

    discovery.start();

    AndroidNotify::requestPermission();   // POST_NOTIFICATIONS (Android 13+)

    TransferManager transfer(deviceId, deviceName);
    transfer.setProfileDefaults(defName);
    // Visibilidad para otros dispositivos (persistida): aplicar y seguir cambios.
    discovery.setVisible(transfer.discoverable());
    QObject::connect(&transfer, &TransferManager::discoverableChanged, &discovery,
                     [&discovery, &transfer]() { discovery.setVisible(transfer.discoverable()); });
    // Renombrar / cambiar avatar en caliente actualiza el anuncio de descubrimiento.
    QObject::connect(&transfer, &TransferManager::deviceNameChanged, &discovery,
                     [&discovery, &transfer]() { discovery.setSelfName(transfer.deviceName()); });
    // Miniatura del avatar (foto de perfil) difundida a los demás dispositivos.
    QObject::connect(&transfer, &TransferManager::avatarImageChanged, &discovery,
                     [&discovery, &transfer]() { discovery.setSelfAvatarThumb(transfer.avatarThumb()); });
    discovery.setSelfAvatarThumb(transfer.avatarThumb());   // inicial

    // Icono de ventana según el tema (claro/oscuro), reactivo a cambios.
    auto applyWindowIcon = [&transfer]() {
        AppClass::setWindowIcon(QIcon(transfer.effectiveDark()
            ? QStringLiteral(":/appicon.svg") : QStringLiteral(":/appicon-light.svg")));
    };
    applyWindowIcon();
    QObject::connect(&transfer, &TransferManager::themeChanged, &transfer, applyWindowIcon);
    QObject::connect(&transfer, &TransferManager::systemDarkChanged, &transfer, applyWindowIcon);

    TrayController tray(&transfer);   // bandeja + notificaciones (escritorio)
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Ocultar la ventana a la bandeja no debe cerrar la app; el cierre lo
    // gestiona QML (ocultar si "minimizar a bandeja", o Qt.quit() si no).
    app.setQuitOnLastWindowClosed(false);
#endif

    // --- Idioma (i18n) ---
    // El idioma FUENTE de los textos es español → no hay .qm para "es".
    // Para otros idiomas se carga vorlan_<lang>.qm de los recursos. "" = sistema.
    // Si el idioma solicitado no está disponible (no existe el .qm), se hace
    // fallback a inglés ("en") como idioma base más extendido. Si tampoco hay
    // traducción para en (solo debería pasar en builds sin i18n), se usan los
    // literales originales en español.
    QQmlApplicationEngine engine;
    QTranslator appTranslator;
    auto applyLanguage = [&app, &appTranslator](const QString &code) {
        app.removeTranslator(&appTranslator);
        const QString lang = (code.isEmpty() ? QLocale::system().name() : code).left(2);
        if (lang != QLatin1String("es")) {
            // 1) intentar el idioma solicitado
            if (!appTranslator.load(QStringLiteral("vorlan_") + lang, QStringLiteral(":/i18n"))) {
                // 2) idioma no disponible → fallback a inglés
                if (!appTranslator.load(QStringLiteral("vorlan_en"), QStringLiteral(":/i18n"))) {
                    qWarning("No translations available for the requested language");
                }
            }
            app.installTranslator(&appTranslator);
        }
    };
    applyLanguage(transfer.language());   // inicial, antes de cargar la UI
    QObject::connect(&transfer, &TransferManager::languageChanged, &engine,
                     [&applyLanguage, &transfer, &engine]() {
                         applyLanguage(transfer.language());
                         engine.retranslate();   // re-traduce todos los qsTr() en vivo
                     });

    engine.rootContext()->setContextProperty("deviceModel", &devices);
    engine.rootContext()->setContextProperty("transfer", &transfer);
    engine.rootContext()->setContextProperty("tray", &tray);
    engine.loadFromModule("Vorlan", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Restaurar geometría de la ventana (tamaño y posición del último cierre).
    QWindow *mainWindow = nullptr;
    if (auto roots = engine.rootObjects(); !roots.isEmpty())
        mainWindow = qobject_cast<QWindow *>(roots.first());
    if (mainWindow) {
        const int w = settings.value("windowWidth", 350).toInt();
        const int h = settings.value("windowHeight", 520).toInt();
        mainWindow->resize(w, h);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        if (settings.contains("windowX") && settings.contains("windowY")) {
            int x = settings.value("windowX").toInt();
            int y = settings.value("windowY").toInt();
            // Asegurar que la ventana completa sea visible dentro del escritorio.
            x = qBound(screen.left(), x, screen.right() - w);
            y = qBound(screen.top(), y, screen.bottom() - h);
            mainWindow->setPosition(x, y);
        } else {
            // Primera vez: centrar en pantalla.
            mainWindow->setPosition((screen.width() - w) / 2, (screen.height() - h) / 2);
        }
        mainWindow->show();
    }
    // Guardar geometría al salir.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&mainWindow]() {
        if (!mainWindow) return;
        QSettings s;
        s.setValue("windowWidth", mainWindow->width());
        s.setValue("windowHeight", mainWindow->height());
        s.setValue("windowX", mainWindow->x());
        s.setValue("windowY", mainWindow->y());
    });

    // Otra instancia intentó abrirse: mostramos y activamos nuestra ventana.
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &engine,
                     [&instanceServer, &engine]() {
        while (QLocalSocket *s = instanceServer.nextPendingConnection()) {
            s->disconnectFromServer();
            s->deleteLater();
        }
        const auto roots = engine.rootObjects();
        if (!roots.isEmpty()) {
            if (auto *w = qobject_cast<QWindow *>(roots.first())) {
                w->show();
                w->raise();
                w->requestActivate();
            }
        }
    });
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // En móvil: mostrar la ventana ahora que el QML está cargado.
    if (auto roots = engine.rootObjects(); !roots.isEmpty())
        if (auto *w = qobject_cast<QWindow *>(roots.first()))
            w->show();
#endif

    // Ganchos de prueba (verificación automatizada; no afectan la UX):
    //   VORLAN_AUTOACCEPT=1            -> acepta entrantes sin preguntar
    //   VORLAN_SEND=host,port,ruta[,ruta2,…]  -> envía esos archivos/carpetas al arrancar
    //   VORLAN_CANCEL_MS=<ms>          -> cancela la transferencia tras N ms
    if (qEnvironmentVariableIsSet("VORLAN_AUTOACCEPT"))
        transfer.setAutoAccept(true);
    const QByteArray sendSpec = qgetenv("VORLAN_SEND");
    if (!sendSpec.isEmpty()) {
        const QList<QByteArray> parts = sendSpec.split(',');
        if (parts.size() >= 3) {
            const QString host = QString::fromUtf8(parts[0]);
            const int port = parts[1].toInt();
            QStringList paths;
            for (int i = 2; i < parts.size(); ++i)
                paths << QString::fromUtf8(parts[i]);
            QTimer::singleShot(800, &transfer, [&transfer, host, port, paths]() {
                transfer.enqueueSendPaths(host, port, QString(), QString(), paths);
            });
        }
    }
    // VORLAN_SENDTEXT="host,port,texto"  o  VORLAN_SENDTEXT="host,port" + VORLAN_TEXT="…"
    // (la segunda forma permite texto con comas).
    const QByteArray sendTextSpec = qgetenv("VORLAN_SENDTEXT");
    if (!sendTextSpec.isEmpty()) {
        const QList<QByteArray> parts = sendTextSpec.split(',');
        if (parts.size() >= 2) {
            const QString host = QString::fromUtf8(parts[0]);
            const int port = parts[1].toInt();
            const QString text = parts.size() >= 3 ? QString::fromUtf8(parts[2])
                                                   : qEnvironmentVariable("VORLAN_TEXT");
            QTimer::singleShot(800, &transfer, [&transfer, host, port, text]() {
                transfer.sendText(host, port, text);
            });
        }
    }
    if (qEnvironmentVariableIsSet("VORLAN_CANCEL_MS")) {
        const int ms = qEnvironmentVariable("VORLAN_CANCEL_MS").toInt();
        QTimer::singleShot(ms, &transfer, [&transfer]() { transfer.cancel(); });
    }
    // VORLAN_ENQUEUE="host,port,path,count" -> encola N envíos (prueba de la cola)
    const QByteArray enqSpec = qgetenv("VORLAN_ENQUEUE");
    if (!enqSpec.isEmpty()) {
        const QList<QByteArray> parts = enqSpec.split(',');
        if (parts.size() >= 4) {
            const QString host = QString::fromUtf8(parts[0]);
            const int port = parts[1].toInt();
            const QString path = QString::fromUtf8(parts[2]);
            const int count = parts[3].toInt();
            for (int k = 0; k < count; ++k) {
                const QString nm = QStringLiteral("Equipo %1").arg(k + 1);
                QTimer::singleShot(800 + k * 100, &transfer, [&transfer, host, port, nm, path]() {
                    transfer.enqueueSend(host, port, nm, QStringLiteral("linux"),
                                         QList<QUrl>{QUrl::fromLocalFile(path)});
                });
            }
        }
    }

    return app.exec();
}
