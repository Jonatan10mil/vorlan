#include "AndroidDevice.h"

#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace {
QString buildField(const char *field)
{
    QJniObject s = QJniObject::getStaticObjectField(
        "android/os/Build", field, "Ljava/lang/String;");
    return s.isValid() ? s.toString() : QString();
}
}

namespace AndroidDevice {

QString deviceModel()
{
    QString man = buildField("MANUFACTURER");
    QString mod = buildField("MODEL");
    if (!man.isEmpty())
        man[0] = man[0].toUpper();
    if (mod.isEmpty())
        return man;
    if (mod.startsWith(man, Qt::CaseInsensitive))   // evitar "Samsung Samsung…"
        return mod;
    return (man.isEmpty() ? mod : man + " " + mod);
}

QString deviceName()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        QJniObject resolver = context.callObjectMethod(
            "getContentResolver", "()Landroid/content/ContentResolver;");
        if (resolver.isValid()) {
            QJniObject key = QJniObject::fromString(QStringLiteral("device_name"));
            QJniObject name = QJniObject::callStaticObjectMethod(
                "android/provider/Settings$Global", "getString",
                "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;",
                resolver.object(), key.object<jstring>());
            if (name.isValid()) {
                const QString n = name.toString();
                if (!n.isEmpty())
                    return n;
            }
        }
    }
    return deviceModel();
}

bool isTelevision()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return false;
    // UiModeManager.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION (4)
    QJniObject svcName = QJniObject::fromString(QStringLiteral("uimode"));
    QJniObject uiMode = context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
        svcName.object<jstring>());
    if (!uiMode.isValid())
        return false;
    const jint type = uiMode.callMethod<jint>("getCurrentModeType", "()I");
    return type == 4;   // Configuration.UI_MODE_TYPE_TELEVISION
}

} // namespace AndroidDevice

#else  // no-Android

namespace AndroidDevice {
QString deviceName() { return QString(); }
QString deviceModel() { return QString(); }
bool isTelevision() { return false; }
}

#endif
