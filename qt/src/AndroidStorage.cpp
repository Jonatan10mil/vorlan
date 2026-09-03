#include "AndroidStorage.h"

#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>
#include <QtCore/private/qandroidextras_p.h>
#include <QStandardPaths>
#include <QDir>

namespace {
QJniObject androidContext()
{
    return QNativeInterface::QAndroidApplication::context();
}
}

namespace AndroidStorage {

QString saveToDownloads(const QString &srcPath, const QString &relPath)
{
    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly))
        return QString();

    QJniObject context = androidContext();
    if (!context.isValid())
        return QString();

    // Separar nombre y subcarpeta relativa.
    const QString name = QFileInfo(relPath).fileName();
    QString subdir = QFileInfo(relPath).path();   // "" o "Vorlan/sub"
    if (subdir == QLatin1String("."))
        subdir.clear();

    const QString dirDownloads = QStringLiteral("Download");
    const QString relativePath = subdir.isEmpty()
        ? dirDownloads : dirDownloads + "/" + subdir;

    // ContentValues
    QJniObject values("android/content/ContentValues");
    if (!values.isValid())
        return QString();

    QJniObject kName = QJniObject::fromString(QStringLiteral("_display_name"));
    QJniObject vName = QJniObject::fromString(name);
    values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V",
                            kName.object<jstring>(), vName.object<jstring>());

    // MIME type: needed on Android 10+ or MediaStore renames the file (e.g. 0.bin).
    int dot = name.lastIndexOf('.');
    QString ext = (dot > 0) ? name.mid(dot + 1).toLower() : QString();
    QString mime;
    if (!ext.isEmpty()) {
        QJniObject mimeMap = QJniObject::getStaticObjectField(
            "android/webkit/MimeTypeMap", "getSingleton",
            "()Landroid/webkit/MimeTypeMap;");
        if (mimeMap.isValid()) {
            QJniObject jext = QJniObject::fromString(ext);
            QJniObject jmime = mimeMap.callObjectMethod(
                "getMimeTypeFromExtension", "(Ljava/lang/String;)Ljava/lang/String;",
                jext.object<jstring>());
            if (jmime.isValid()) mime = jmime.toString();
        }
    }
    if (mime.isEmpty()) mime = QStringLiteral("application/octet-stream");
    QJniObject kMime = QJniObject::fromString(QStringLiteral("mime_type"));
    QJniObject vMime = QJniObject::fromString(mime);
    values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V",
                            kMime.object<jstring>(), vMime.object<jstring>());

    QJniObject kRel = QJniObject::fromString(QStringLiteral("relative_path"));
    QJniObject vRel = QJniObject::fromString(relativePath);
    values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V",
                            kRel.object<jstring>(), vRel.object<jstring>());

    QJniObject kPending = QJniObject::fromString(QStringLiteral("is_pending"));
    QJniObject onePending("java/lang/Integer", "(I)V", 1);
    values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/Integer;)V",
                            kPending.object<jstring>(), onePending.object());

    // Uri de MediaStore.Downloads.EXTERNAL_CONTENT_URI
    QJniObject collection = QJniObject::getStaticObjectField(
        "android/provider/MediaStore$Downloads", "EXTERNAL_CONTENT_URI",
        "Landroid/net/Uri;");
    if (!collection.isValid())
        return QString();

    QJniObject resolver = context.callObjectMethod(
        "getContentResolver", "()Landroid/content/ContentResolver;");
    if (!resolver.isValid())
        return QString();

    QJniObject uri = resolver.callObjectMethod(
        "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
        collection.object(), values.object());
    if (!uri.isValid())
        return QString();

    QJniObject os = resolver.callObjectMethod(
        "openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;", uri.object());
    if (!os.isValid())
        return QString();

    // Copiar el archivo por trozos hacia el OutputStream (vía jbyteArray).
    QJniEnvironment env;
    const int chunk = 256 * 1024;
    jbyteArray jbuf = env->NewByteArray(chunk);
    if (!jbuf)
        return QString();
    bool ok = true;
    QByteArray buffer(chunk, Qt::Uninitialized);
    while (!src.atEnd()) {
        const qint64 n = src.read(buffer.data(), chunk);
        if (n <= 0)
            break;
        env->SetByteArrayRegion(jbuf, 0, static_cast<jsize>(n),
                                reinterpret_cast<const jbyte *>(buffer.constData()));
        os.callMethod<void>("write", "([BII)V", jbuf, jint(0), jint(n));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            ok = false;
            break;
        }
    }
    os.callMethod<void>("flush");
    os.callMethod<void>("close");
    env->DeleteLocalRef(jbuf);

    // Quitar is_pending para publicarlo.
    QJniObject values2("android/content/ContentValues");
    QJniObject zeroPending("java/lang/Integer", "(I)V", 0);
    values2.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/Integer;)V",
                             kPending.object<jstring>(), zeroPending.object());
    resolver.callMethod<jint>(
        "update",
        "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I",
        uri.object(), values2.object(), nullptr, nullptr);

    if (!ok) {
        resolver.callMethod<jint>("delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I",
                                  uri.object(), nullptr, nullptr);
        return QString();
    }
    QJniObject s = uri.callObjectMethod("toString", "()Ljava/lang/String;");
    return s.isValid() ? s.toString() : QString();
}

// ¿Hay alguna app que atienda este intent? En Android TV suele faltar el selector
// de archivos/carpetas (DocumentsUI); sin esta comprobación, startActivity lanzaría
// ActivityNotFoundException.
static bool canResolveIntent(const QJniObject &intent)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return false;
    QJniObject pm = ctx.callObjectMethod("getPackageManager",
                                         "()Landroid/content/pm/PackageManager;");
    if (!pm.isValid())
        return false;
    QJniObject comp = intent.callObjectMethod(
        "resolveActivity", "(Landroid/content/pm/PackageManager;)Landroid/content/ComponentName;",
        pm.object());
    return comp.isValid();
}

void pickFolder(std::function<void(const QString &)> cb)
{
    // Intent ACTION_OPEN_DOCUMENT_TREE (selector de carpetas del sistema).
    QJniObject action = QJniObject::fromString(
        QStringLiteral("android.intent.action.OPEN_DOCUMENT_TREE"));
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      action.object<jstring>());
    if (!intent.isValid() || !canResolveIntent(intent)) { cb(QString()); return; }

    QtAndroidPrivate::startActivity(
        intent, 0x5AF0,
        [cb](int, int resultCode, const QJniObject &data) {
            constexpr int RESULT_OK = -1;
            if (resultCode != RESULT_OK || !data.isValid()) { cb(QString()); return; }
            QJniObject uri = data.callObjectMethod("getData", "()Landroid/net/Uri;");
            if (!uri.isValid()) { cb(QString()); return; }

            // Tomar permiso persistente de lectura+escritura sobre el árbol.
            QJniObject ctx = androidContext();
            QJniObject resolver = ctx.callObjectMethod(
                "getContentResolver", "()Landroid/content/ContentResolver;");
            const jint READ = 1, WRITE = 2, PERSISTABLE = 0x40;
            const jint granted = data.callMethod<jint>("getFlags", "()I");
            // Solo si el sistema otorgó FLAG_GRANT_PERSISTABLE_URI_PERMISSION
            if ((granted & PERSISTABLE) == 0) {
                qWarning() << "[AndroidStorage] sin FLAG_GRANT_PERSISTABLE, no se toma permiso persistente";
            } else {
                resolver.callMethod<void>(
                    "takePersistableUriPermission", "(Landroid/net/Uri;I)V",
                    uri.object(), jint(granted & (READ | WRITE)));
            }

            QJniObject s = uri.callObjectMethod("toString", "()Ljava/lang/String;");
            cb(s.isValid() ? s.toString() : QString());
        });
}

void pickFiles(std::function<void(const QStringList &)> cb)
{
    QJniObject action = QJniObject::fromString(
        QStringLiteral("android.intent.action.OPEN_DOCUMENT"));
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      action.object<jstring>());
    if (!intent.isValid()) { cb(QStringList()); return; }

    QJniObject cat = QJniObject::fromString(QStringLiteral("android.intent.category.OPENABLE"));
    intent.callObjectMethod("addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
                            cat.object<jstring>());
    QJniObject type = QJniObject::fromString(QStringLiteral("*/*"));
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;",
                            type.object<jstring>());
    QJniObject extra = QJniObject::fromString(QStringLiteral("android.intent.extra.ALLOW_MULTIPLE"));
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Z)Landroid/content/Intent;",
                            extra.object<jstring>(), jboolean(true));
    if (!canResolveIntent(intent)) { cb(QStringList()); return; }

    QtAndroidPrivate::startActivity(
        intent, 0x5AF1,
        [cb](int, int resultCode, const QJniObject &data) {
            constexpr int RESULT_OK = -1;
            QStringList out;
            if (resultCode != RESULT_OK || !data.isValid()) { cb(out); return; }

            auto uriToString = [](const QJniObject &uri) -> QString {
                if (!uri.isValid())
                    return QString();
                QJniObject s = uri.callObjectMethod("toString", "()Ljava/lang/String;");
                return s.isValid() ? s.toString() : QString();
            };

            // Selección MÚLTIPLE → las URIs vienen en ClipData.
            QJniObject clip = data.callObjectMethod("getClipData", "()Landroid/content/ClipData;");
            if (clip.isValid()) {
                const jint n = clip.callMethod<jint>("getItemCount", "()I");
                for (jint i = 0; i < n; ++i) {
                    QJniObject item = clip.callObjectMethod(
                        "getItemAt", "(I)Landroid/content/ClipData$Item;", i);
                    if (!item.isValid())
                        continue;
                    const QString s = uriToString(
                        item.callObjectMethod("getUri", "()Landroid/net/Uri;"));
                    if (!s.isEmpty())
                        out << s;
                }
            } else {   // selección simple
                const QString s = uriToString(
                    data.callObjectMethod("getData", "()Landroid/net/Uri;"));
                if (!s.isEmpty())
                    out << s;
            }
            cb(out);
        });
}

QString saveToTree(const QString &treeUri, const QString &relPath, const QString &srcPath)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return QString();
    QJniObject s = QJniObject::callStaticObjectMethod(
        "net/vorlan/SafStorage", "saveToTree",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        ctx.object(),
        QJniObject::fromString(treeUri).object<jstring>(),
        QJniObject::fromString(relPath).object<jstring>(),
        QJniObject::fromString(srcPath).object<jstring>());
    return s.isValid() ? s.toString() : QString();
}

QString contentToCache(const QString &uri)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid() || uri.isEmpty())
        return QString();
    QJniObject s = QJniObject::callStaticObjectMethod(
        "net/vorlan/SafStorage", "copyOutgoing",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        ctx.object(), QJniObject::fromString(uri).object<jstring>());
    return s.isValid() ? s.toString() : QString();
}

void clearOutgoingCache()
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        "net/vorlan/SafStorage", "clearOutgoing",
        "(Landroid/content/Context;)V", ctx.object());
}

void clearSharedCache()
{
#ifdef Q_OS_ANDROID
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) return;
    QDir(base + "/shared").removeRecursively();
    QDir(base + "/outgoing").removeRecursively();
    QDir(base + "/apks").removeRecursively();
    QDir(base + "/appicons").removeRecursively();
#endif
}

QString displayNameOf(const QString &uri)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid() || uri.isEmpty())
        return QString();
    QJniObject s = QJniObject::callStaticObjectMethod(
        "net/vorlan/Opener", "nameOf",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        ctx.object(), QJniObject::fromString(uri).object<jstring>());
    return s.isValid() ? s.toString() : QString();
}

void openContent(const QString &uriString)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid() || uriString.isEmpty())
        return;
    QJniObject::callStaticMethod<void>(
        "net/vorlan/Opener", "openContent",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        ctx.object(), QJniObject::fromString(uriString).object<jstring>());
}

void shareContent(const QString &pathOrUri)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid() || pathOrUri.isEmpty())
        return;
    QJniObject::callStaticMethod<void>(
        "net/vorlan/Opener", "shareContent",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        ctx.object(), QJniObject::fromString(pathOrUri).object<jstring>());
}

bool isBatteryExempt()
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return false;
    const QString pkg = ctx.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();
    QJniObject pm = ctx.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
        QJniObject::fromString(QStringLiteral("power")).object<jstring>());
    return pm.isValid()
        && pm.callMethod<jboolean>("isIgnoringBatteryOptimizations", "(Ljava/lang/String;)Z",
                                   QJniObject::fromString(pkg).object<jstring>());
}

void requestBatteryExemption()
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return;
    if (isBatteryExempt())
        return;   // ya está exenta
    const QString pkg = ctx.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
        QJniObject::fromString(QStringLiteral("android.settings.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS"))
            .object<jstring>());
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(QStringLiteral("package:") + pkg).object<jstring>());
    intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", uri.object());
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(0x10000000)); // NEW_TASK
    ctx.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
}

void openFolder(const QString &treeUri)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        "net/vorlan/Opener", "openFolder",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        ctx.object(), QJniObject::fromString(treeUri).object<jstring>());
}

QString installedApps()
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return QStringLiteral("[]");
    QJniObject s = QJniObject::callStaticObjectMethod(
        "net/vorlan/AppList", "installedApps",
        "(Landroid/content/Context;)Ljava/lang/String;", ctx.object());
    return s.isValid() ? s.toString() : QStringLiteral("[]");
}

QString stageApk(const QString &apkPath, const QString &label)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return QString();
    QJniObject s = QJniObject::callStaticObjectMethod(
        "net/vorlan/AppList", "stageApk",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        ctx.object(),
        QJniObject::fromString(apkPath).object<jstring>(),
        QJniObject::fromString(label).object<jstring>());
    return s.isValid() ? s.toString() : QString();
}

static QString clipboardCall(const char *method)
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return QString();
    QJniObject s = QJniObject::callStaticObjectMethod(
        "net/vorlan/Clipboard", method,
        "(Landroid/content/Context;)Ljava/lang/String;", ctx.object());
    return s.isValid() ? s.toString() : QString();
}
QString clipboardImage() { return clipboardCall("stageImage"); }
QString clipboardText()  { return clipboardCall("text"); }

void applySystemBars(bool lightBackground)
{
    QJniObject act = androidContext();   // la QtActivity es un Activity
    if (!act.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        "net/vorlan/SystemBars", "apply",
        "(Landroid/app/Activity;Z)V", act.object(), jboolean(lightBackground));
}

QString takeSharedFiles()
{
    QJniObject act = androidContext();
    if (!act.isValid())
        return QString();
    QJniObject s = QJniObject::callStaticObjectMethod(
        "net/vorlan/ShareIntent", "take",
        "(Landroid/app/Activity;)Ljava/lang/String;", act.object());
    return s.isValid() ? s.toString() : QString();
}

bool takeOpenReceived()
{
    return QJniObject::callStaticMethod<jboolean>(
        "net/vorlan/MainActivity", "takeOpenReceived", "()Z");
}

QString takeSharedText()
{
    QJniObject s = QJniObject::callStaticObjectMethod(
        "net/vorlan/ShareIntent", "takeText", "()Ljava/lang/String;");
    return s.isValid() ? s.toString() : QString();
}

bool hasStorageManagerPermission()
{
    return QJniObject::callStaticMethod<jboolean>(
        "net/vorlan/StoragePermission", "isGranted", "()Z");
}

void requestStorageManagerPermission()
{
    QJniObject ctx = androidContext();
    if (!ctx.isValid()) return;
    QJniObject::callStaticMethod<void>(
        "net/vorlan/StoragePermission", "request",
        "(Landroid/content/Context;)V",
        ctx.object());
}

bool folderExistsInTree(const QString &treeUri, const QString &folderName)
{
    if (treeUri.isEmpty() || folderName.isEmpty()) return false;
    QJniObject ctx = androidContext();
    if (!ctx.isValid()) return false;
    QJniObject jTreeUri = QJniObject::fromString(treeUri);
    QJniObject jName   = QJniObject::fromString(folderName);
    return QJniObject::callStaticMethod<jboolean>(
        "net/vorlan/SafStorage", "folderExistsInTree",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        ctx.object(), jTreeUri.object<jstring>(), jName.object<jstring>());
}

bool existsInDownloads(const QString &name)
{
    if (name.isEmpty()) return false;

    QJniObject context = androidContext();
    if (!context.isValid()) return false;

    QJniObject resolver = context.callObjectMethod(
        "getContentResolver", "()Landroid/content/ContentResolver;");
    if (!resolver.isValid()) return false;

    QJniObject collection = QJniObject::getStaticObjectField(
        "android/provider/MediaStore$Downloads", "EXTERNAL_CONTENT_URI",
        "Landroid/net/Uri;");
    if (!collection.isValid()) return false;

    // saveToDownloads almacena relative_path="Download/Vorlan/<subfolder>".
    // Buscar cualquier archivo cuyo relative_path contenga "Vorlan/<name>".
    // Usamos MediaStore.Files (más universal) con LIKE para cubrir variaciones
    // del path ("Download/Vorlan/X", "Vorlan/X", etc.).
    const QString likePat = QStringLiteral("%/Vorlan/%1/%").arg(name);
    const QString eqPat   = QStringLiteral("%/Vorlan/%1").arg(name);

    QJniEnvironment env;

    // Projection
    QJniObject colId = QJniObject::fromString(QStringLiteral("_id"));
    jobjectArray proj = env->NewObjectArray(1, env->FindClass("java/lang/String"), nullptr);
    env->SetObjectArrayElement(proj, 0, colId.object<jstring>());

    // Selection: relative_path LIKE '%/Vorlan/name/%' OR relative_path = '%/Vorlan/name'
    // Selection: ambas condiciones usan LIKE (no =) porque los patrones
    // contienen '%' que solo funciona con LIKE.
    QJniObject selection = QJniObject::fromString(
        QStringLiteral("relative_path LIKE ? OR relative_path LIKE ?"));

    // Args — IMPORTANTE: guardar los QJniObject en variables para que las
    // referencias JNI no se destruyan antes de la query.
    QJniObject argLike = QJniObject::fromString(likePat);
    QJniObject argEq   = QJniObject::fromString(eqPat);
    jobjectArray args = env->NewObjectArray(2, env->FindClass("java/lang/String"), nullptr);
    env->SetObjectArrayElement(args, 0, argLike.object<jstring>());
    env->SetObjectArrayElement(args, 1, argEq.object<jstring>());

    QJniObject cursor = resolver.callObjectMethod(
        "query",
        "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
        collection.object(), proj, selection.object<jstring>(), args, nullptr);

    bool found = false;
    if (cursor.isValid()) {
        found = cursor.callMethod<jint>("getCount") > 0;
        cursor.callMethod<void>("close");
    }
    env->DeleteLocalRef(proj);
    env->DeleteLocalRef(args);
    return found;
}

} // namespace AndroidStorage

#else  // no-Android

namespace AndroidStorage {
QString saveToDownloads(const QString &, const QString &) { return QString(); }
void pickFolder(std::function<void(const QString &)> cb) { cb(QString()); }
void pickFiles(std::function<void(const QStringList &)> cb) { cb(QStringList()); }
QString saveToTree(const QString &, const QString &, const QString &) { return QString(); }
QString contentToCache(const QString &) { return QString(); }
void clearOutgoingCache() {}
void clearSharedCache() {}
QString displayNameOf(const QString &) { return QString(); }
void openContent(const QString &) {}
void shareContent(const QString &) {}
void requestBatteryExemption() {}
bool isBatteryExempt() { return true; }
void openFolder(const QString &) {}
QString installedApps() { return QStringLiteral("[]"); }
QString stageApk(const QString &, const QString &) { return QString(); }
QString clipboardImage() { return QString(); }
QString clipboardText() { return QString(); }
void applySystemBars(bool) {}
QString takeSharedFiles() { return QString(); }
QString takeSharedText() { return QString(); }
bool takeOpenReceived() { return false; }
bool existsInDownloads(const QString &) { return false; }
bool folderExistsInTree(const QString &, const QString &) { return false; }
bool hasStorageManagerPermission() { return true; }
void requestStorageManagerPermission() {}
}

#endif
