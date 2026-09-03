#include "FileActions.h"
#include "AndroidStorage.h"

#include <QFileInfo>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>

#ifndef Q_OS_ANDROID
#include <QDesktopServices>
#include <QProcess>
#include <QCoreApplication>
#include <QProcessEnvironment>

// Abre una ruta con la app predeterminada del sistema.
// En Linux NO usamos QDesktopServices directamente: cuando la app va empaquetada
// (.deb con Qt embebido) el wrapper exporta LD_LIBRARY_PATH/QT_PLUGIN_PATH/QML*_IMPORT_PATH
// hacia /opt/vorlan, y el proceso hijo (p.ej. el visor de imágenes del sistema) heredaría
// esas rutas y cargaría NUESTRAS librerías Qt → "undefined symbol". Por eso lanzamos
// xdg-open con un entorno saneado, quitando las rutas del paquete.
static bool desktopOpen(const QString &target)
{
#ifdef Q_OS_LINUX
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString prefix = QDir(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/..")).canonicalPath();
    for (const QString &name : { QStringLiteral("LD_LIBRARY_PATH"),
                                 QStringLiteral("QT_PLUGIN_PATH"),
                                 QStringLiteral("QML2_IMPORT_PATH"),
                                 QStringLiteral("QML_IMPORT_PATH") }) {
        if (!env.contains(name))
            continue;
        QStringList kept;
        const QStringList parts = env.value(name).split(QLatin1Char(':'), Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            if (!prefix.isEmpty() && (p == prefix || p.startsWith(prefix + QLatin1Char('/'))))
                continue;   // descartar rutas del paquete
            kept << p;
        }
        if (kept.isEmpty())
            env.remove(name);
        else
            env.insert(name, kept.join(QLatin1Char(':')));
    }
    QProcess p;
    p.setProgram(QStringLiteral("xdg-open"));
    p.setArguments({ target });
    p.setProcessEnvironment(env);
    return p.startDetached();
#elif defined(Q_OS_WIN)
    // En Windows usar explorer directamente es más fiable que QDesktopServices
    // para carpetas con espacios o caracteres especiales.
    return QProcess::startDetached(QStringLiteral("explorer"),
        { QDir::toNativeSeparators(target) });
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(target));
#endif
}

// Abre el gestor de archivos con el ARCHIVO SELECCIONADO (revelar/mostrar en carpeta).
// Devuelve false si no se pudo → el llamador abre la carpeta contenedora como respaldo.
static bool desktopReveal(const QString &path)
{
#if defined(Q_OS_LINUX)
    // Estándar freedesktop: selecciona el elemento en Nautilus/Dolphin/Nemo/Files…
    const QString uri = QUrl::fromLocalFile(path).toString();
    QProcess p;
    p.setProgram(QStringLiteral("dbus-send"));
    p.setArguments({
        QStringLiteral("--session"),
        QStringLiteral("--dest=org.freedesktop.FileManager1"),
        QStringLiteral("--type=method_call"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
        QStringLiteral("array:string:") + uri,
        QStringLiteral("string:vorlan")
    });
    p.start();
    return p.waitForFinished(3000)
           && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
#elif defined(Q_OS_WIN)
    const QString native = QDir::toNativeSeparators(QDir::cleanPath(path));
    QFileInfo fi(native);
    // PASOS SEPARADOS: Qt envuelve en comillas si el arg tiene espacios,
    // y explorer NO entiende "/select,D:\path\file (7).jpg".
    // Con dos args separados Qt produce: explorer /select, "D:\path\file (7).jpg"
    QStringList args;
    args << QStringLiteral("/select,") << native;
    if (QProcess::startDetached(QStringLiteral("explorer"), args)) {
        return true;
    }
    QString folder = fi.absolutePath();
    return QProcess::startDetached(QStringLiteral("explorer"), { QDir::toNativeSeparators(folder) });
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"), { QStringLiteral("-R"), path });
#else
    Q_UNUSED(path)
    return false;
#endif
}
#endif // !Q_OS_ANDROID

namespace FileActions {

// Normaliza rutas "file://..." viejas a ruta local y limpia el path.
// En Android se preserva "content://", en escritorio se limpia con QDir::cleanPath.
static QString normalizeLocalPath(const QString &p)
{
    if (p.startsWith(QLatin1String("file://"))) {
        const QString local = QUrl(p).toLocalFile();
        if (!local.isEmpty())
            return QDir::cleanPath(local);
    }
    // Solo limpiar rutas locales; content:// se deja intacto (Android).
    if (!p.startsWith(QLatin1String("content://")) && !p.isEmpty())
        return QDir::cleanPath(p);
    return p;
}

bool isDir(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1String("content://")))
        return false;
    QString p = normalizeLocalPath(path);
    return QFileInfo(p).isDir();
}

QString downloadsPath(const QString &downloadDir)
{
#ifdef Q_OS_ANDROID
    if (downloadDir.startsWith(QLatin1String("content://"))) {
        // Mostrar un nombre legible del árbol elegido (p.ej. "primary:Documents" → "Documents").
        const QString dec = QUrl::fromPercentEncoding(downloadDir.toUtf8());
        const int colon = dec.lastIndexOf(':');
        const QString tail = colon >= 0 ? dec.mid(colon + 1) : dec;
        return tail.isEmpty() ? QStringLiteral("Carpeta elegida") : tail;
    }
    return QStringLiteral("Descargas/Vorlan");
#else
    QString norm = normalizeLocalPath(downloadDir);
    if (!norm.isEmpty())
        return norm;
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir::cleanPath(base + "/Vorlan");
#endif
}

void openDownloads(const QString &downloadDir)
{
#ifdef Q_OS_ANDROID
    // Carpeta SAF elegida (content://) o la pantalla de Descargas del sistema.
    AndroidStorage::openFolder(downloadDir.startsWith(QLatin1String("content://"))
                                   ? downloadDir : QString());
#else
    const QString p = downloadsPath(downloadDir);
    QDir().mkpath(p);
    desktopOpen(p);
#endif
}

void openPath(const QString &path, const QString &downloadDir)
{
#ifdef Q_OS_ANDROID
    if (path.startsWith(QLatin1String("content://"))) {
        AndroidStorage::openContent(path);   // ACTION_VIEW → "abrir con"
        return;
    }
    // En Android sin SAF el path es local temporal, no hay apertura directa.
    openDownloads(downloadDir);
#else
    const QString normPath = normalizeLocalPath(path);
    if (normPath.isEmpty() || !QFileInfo::exists(normPath)) {
        openDownloads(downloadDir);
        return;
    }
    // Abrir con la app predeterminada; si no hay ninguna asociada, abrir la carpeta.
    if (!::desktopOpen(normPath))
        openContaining(normPath, downloadDir);
#endif
}

void openContaining(const QString &path, const QString &downloadDir)
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(path)
    openDownloads(downloadDir);   // en Android/TV abre la pantalla de Descargas / carpeta SAF
#else
    const QString normPath = normalizeLocalPath(path);
    if (normPath.isEmpty()) {
        openDownloads(downloadDir);
        return;
    }
    QFileInfo fi(normPath);
    // Si es un archivo existente, revelarlo SELECCIONADO en el gestor de archivos.
    if (fi.exists() && fi.isFile() && ::desktopReveal(fi.absoluteFilePath()))
        return;
    // Respaldo: abrir la carpeta contenedora directamente.
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (!dir.isEmpty() && QFileInfo::exists(dir)) {
        ::desktopOpen(dir);
        return;
    }
    // Último recurso: abrir la carpeta de descargas configurada (respeta carpeta personalizada).
    openDownloads(downloadDir);
#endif
}

void share(const QString &path)
{
#ifdef Q_OS_ANDROID
    AndroidStorage::shareContent(path);
#else
    Q_UNUSED(path)   // en escritorio el reenvío lo maneja QML (Vorlan a otro equipo)
#endif
}

} // namespace FileActions
